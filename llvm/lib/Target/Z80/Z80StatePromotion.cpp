//===- Z80StatePromotion.cpp - Promote private state
//-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "Z80Subtarget.h"
#include "Z80TargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

using namespace llvm;
using namespace llvm::PatternMatch;

static cl::opt<bool> EnableStatePromotion(
    "z80-promote-private-state", cl::Hidden, cl::init(true),
    cl::desc("Retain private word state across internal calls"));

static cl::opt<bool> CacheBankAddresses(
    "z80-cache-bank-addresses", cl::Hidden, cl::init(true),
    cl::desc("Carry repeated constant byte addresses with private state"));

/* Keep a spare pair for unrelated values. A second invariant address needs
   regional placement to repay the spills it introduces in larger callers. */
static cl::opt<unsigned> BankAddressCount(
    "z80-bank-address-count", cl::Hidden, cl::init(1),
    cl::desc("Maximum invariant addresses carried with private state"));

static cl::opt<bool> RegionalBankAddresses(
    "z80-regional-bank-addresses", cl::Hidden, cl::init(true),
    cl::desc("Cache a second byte address within repeated leaf-call regions"));

namespace {
using Functions = SmallSetVector<Function *, 16>;

/// Return the numeric address of a direct byte access in the default space.
static ConstantInt *absoluteByteAddress(Instruction &I) {
  Value *P = nullptr;
  if (auto *L = dyn_cast<LoadInst>(&I)) {
    if (L->getType()->isIntegerTy(8))
      P = L->getPointerOperand();
  } else if (auto *S = dyn_cast<StoreInst>(&I)) {
    if (S->getValueOperand()->getType()->isIntegerTy(8))
      P = S->getPointerOperand();
  }
  auto *C = dyn_cast_or_null<ConstantExpr>(P);
  if (!C || C->getOpcode() != Instruction::IntToPtr ||
      C->getType()->getPointerAddressSpace())
    return nullptr;
  auto *Address = dyn_cast<ConstantInt>(C->getOperand(0));
  return Address && Address->getType()->isIntegerTy(16) ? Address : nullptr;
}

/// Recognize updates whose arithmetic can execute on an alternate pair.
/// Word increments and byte-wrapping increments preserve bank residency;
/// arbitrary masks require primary byte registers and end that residency.
bool isBankUpdate(Value *V, GlobalVariable &G) {
  auto FromState = [&](Value *V) {
    auto *L = dyn_cast<LoadInst>(V);
    return L && L->getPointerOperand() == &G;
  };
  if (isa<ConstantInt>(V) || FromState(V))
    return true;
  Value *Base = nullptr;
  ConstantInt *Step = nullptr;
  if (match(V, m_Add(m_Value(Base), m_ConstantInt(Step))) && FromState(Base))
    return Step->isOne() || Step->isMinusOne();
  Value *High = nullptr, *Low = nullptr;
  if (!match(V, m_c_Or(m_And(m_Value(High), m_SpecificInt(0xff00)),
                       m_And(m_Value(Low), m_SpecificInt(0xff)))) ||
      !FromState(High) ||
      !match(Low, m_Add(m_Specific(High), m_ConstantInt(Step))))
    return false;
  return Step->isOne() || Step->isMinusOne();
}

/// Cache a second address for repeated calls to a small leaf within a caller.
/// The specialized leaf receives the constant in its third bank argument. The
/// containing caller establishes it locally and preserves the borrowed pair,
/// keeping its incoming interface and its callers' register lifetimes intact.
/// Other callers continue to use the original leaf and register convention.
static bool specializeAddressRegions(Module &M) {
  if (!RegionalBankAddresses)
    return false;
  struct Region {
    Function *Caller;
    Function *Leaf;
    ConstantInt *Address;
  };
  SmallVector<Region, 8> Regions;
  for (Function &F : M) {
    if (F.getCallingConv() != CallingConv::Z80_BankState ||
        !F.hasLocalLinkage() || F.getInstructionCount() > 256)
      continue;
    auto Text = F.getFnAttribute("z80-bank-addresses").getValueAsString();
    if (Text.empty() || Text.contains(','))
      continue;
    Function *Leaf = nullptr;
    unsigned Count = 0;
    bool Valid = true;
    for (Instruction &I : instructions(F))
      if (auto *C = dyn_cast<CallBase>(&I)) {
        auto *Target = C->getCalledFunction();
        if (Target && Target->isIntrinsic() && C->doesNotAccessMemory())
          continue;
        if (!isa<CallInst>(C) || C->isMustTailCall() || C->hasOperandBundles() ||
            !Target || Target->isDeclaration() || Target == &F ||
            (Leaf && Leaf != Target)) {
          Valid = false;
          break;
        }
        Leaf = Target;
        ++Count;
      }
    if (!Valid || Count < 3 || !Leaf || Leaf->arg_size() != 2 ||
        !Leaf->hasLocalLinkage() || Leaf->getInstructionCount() > 32 ||
        Leaf->getCallingConv() != CallingConv::Z80_BankState ||
        Leaf->getFnAttribute("z80-bank-addresses").getValueAsString() != Text ||
        llvm::any_of(instructions(Leaf), [](Instruction &I) {
          return isa<CallBase>(I);
        }))
      continue;
    uint64_t Existing = 0;
    if (Text.getAsInteger(10, Existing))
      continue;
    MapVector<ConstantInt *, unsigned> Counts;
    for (Instruction &I : instructions(Leaf))
      if (auto *A = absoluteByteAddress(I))
        if (A->getZExtValue() != Existing)
          ++Counts[A];
    ConstantInt *Address = nullptr;
    unsigned Best = 1;
    for (auto [A, Uses] : Counts)
      if (Uses > Best) {
        Address = A;
        Best = Uses;
      }
    /* Each indirect byte access saves six T states. Establishing DE' takes
       18, and preserving it across this region takes another 37. Charge
       those once per caller invocation before specializing its leaf calls.
       Calls in mutually exclusive handlers cannot repay a shared setup. */
    if (!Address || Count * Best * 6 <= 55)
      continue;
    SmallVector<ReturnInst *, 4> Returns;
    for (auto &BB : F)
      if (auto *Ret = dyn_cast<ReturnInst>(BB.getTerminator()))
        Returns.push_back(Ret);
    if (Returns.empty())
      continue;
    DominatorTree DT(F);
    unsigned MandatoryCalls = 0;
    for (Instruction &I : instructions(F))
      if (auto *C = dyn_cast<CallInst>(&I))
        if (C->getCalledFunction() == Leaf &&
            llvm::all_of(Returns, [&](const auto *Ret) {
              return DT.dominates(C, Ret);
            }))
          ++MandatoryCalls;
    if (MandatoryCalls * Best * 6 > 55) {
      Regions.push_back({&F, Leaf, Address});
      if (Regions.size() == 8)
        break;
    }
  }
  DenseMap<Function *, Function *> Versions;
  for (auto [Caller, Leaf, Address] : Regions) {
    Caller->addFnAttr("z80-save-region-bank");
    Function *Version = Versions.lookup(Leaf);
    if (!Version) {
      auto *Ty = Address->getType();
      SmallVector<Type *> Params(Leaf->getFunctionType()->params());
      Params.push_back(Ty);
      Version = Function::Create(
          FunctionType::get(Leaf->getReturnType(), Params, false),
          Leaf->getLinkage(), Leaf->getAddressSpace(),
          Leaf->getName() + ".address", &M);
      Version->copyAttributesFrom(Leaf);
      ValueToValueMapTy Map;
      auto Dest = Version->arg_begin();
      for (Argument &A : Leaf->args()) {
        Dest->setName(A.getName());
        Map[&A] = &*Dest++;
      }
      Dest->setName("region.address");
      SmallVector<ReturnInst *, 4> Returns;
      CloneFunctionInto(Version, Leaf, Map,
                        CloneFunctionChangeType::LocalChangesOnly, Returns);
      Version->addParamAttr(2, Attribute::InReg);
      Version->addFnAttr(
          "z80-bank-addresses",
          Leaf->getFnAttribute("z80-bank-addresses").getValueAsString().str() +
              "," + std::to_string(Address->getZExtValue()));
      IRBuilder<> Entry(&*Version->getEntryBlock().getFirstInsertionPt());
      Value *Pointer = Entry.CreateIntToPtr(
          &*Dest, PointerType::getUnqual(M.getContext()), "region.pointer");
      for (Instruction &I : instructions(Version))
        if (absoluteByteAddress(I) == Address)
          I.setOperand(isa<LoadInst>(I) ? 0 : 1, Pointer);
      Versions[Leaf] = Version;
    }
    for (Instruction &I : make_early_inc_range(instructions(Caller)))
      if (auto *C = dyn_cast<CallInst>(&I)) {
        if (C->getCalledFunction() != Leaf)
          continue;
        SmallVector<Value *> Args(C->args());
        Args.push_back(Address);
        IRBuilder<> B(C);
        auto *N = B.CreateCall(Version, Args);
        N->setCallingConv(Version->getCallingConv());
        N->setAttributes(C->getAttributes());
        N->addParamAttr(2, Attribute::InReg);
        N->setTailCallKind(C->getTailCallKind());
        N->setDebugLoc(C->getDebugLoc());
        C->replaceAllUsesWith(N);
        C->eraseFromParent();
      }
  }
  /* Repeated calls to one small leaf can share its address and cursor ranges
     throughout a region. Bound duplication to eight calls into the already
     limited 32-instruction leaf, and retain explicit inlining restrictions.
     The surrounding region still preserves its borrowed bank at the boundary.
   */
  for (auto [Leaf, Version] : Versions) {
    SmallVector<CallInst *, 8> Calls;
    Function *Caller = nullptr;
    bool Local = true;
    for (User *U : Version->users()) {
      auto *C = dyn_cast<CallInst>(U);
      if (!C || C->getCalledFunction() != Version ||
          C->hasFnAttr(Attribute::NoInline) || C->getFunction()->hasOptNone() ||
          (Caller && C->getFunction() != Caller)) {
        Local = false;
        break;
      }
      Caller = C->getFunction();
      Calls.push_back(C);
    }
    if (!Local || Calls.size() < 3 || Calls.size() > 8 ||
        Version->hasFnAttribute(Attribute::NoInline))
      continue;
    for (CallInst *C : Calls) {
      InlineFunctionInfo IFI;
      InlineFunction(*C, IFI);
    }
    if (Version->use_empty())
      Version->eraseFromParent();
  }
  return !Regions.empty();
}

class Z80StatePromotion : public ModulePass {
  const Z80TargetMachine &TM;

  bool eligible(const Function &F) const {
    const auto &ST = *TM.getSubtargetImpl(F);
    return ST.useAlternateRegs() && ST.is16Bit() && !ST.hasEZ80Ops() &&
           !F.isDeclaration() && !F.isVarArg() && !F.hasOptNone() &&
           !F.hasFnAttribute(Attribute::Naked) &&
           !F.hasFnAttribute("interrupt") && !F.hasPersonalityFn() &&
           (F.getCallingConv() == CallingConv::C ||
            F.getCallingConv() == CallingConv::Fast);
  }

public:
  static char ID;
  explicit Z80StatePromotion(const Z80TargetMachine &TM)
      : ModulePass(ID), TM(TM) {}
  StringRef getPassName() const override {
    return "Z80 Private State Promotion";
  }
  bool runOnModule(Module &M) override;
};
} // namespace

char Z80StatePromotion::ID;

/// Carry a private word and frequent constant addresses through internal calls.
/// Calls that may observe memory synchronize the word with its original global.
/// Escaping addresses, atomic accesses and unsupported call sites retain
/// memory.
bool Z80StatePromotion::runOnModule(Module &M) {
  if (!EnableStatePromotion ||
      llvm::none_of(M, [&](Function &F) { return eligible(F); }))
    return false;

  DenseMap<Function *, SmallVector<Function *, 8>> Calls;
  DenseMap<Function *, SmallVector<Function *, 8>> Callers;
  Functions Barriers, Unsupported;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (Instruction &I : instructions(F)) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (!CB)
        continue;
      if (!isa<CallInst>(CB) || CB->hasOperandBundles() || CB->isMustTailCall())
        Unsupported.insert(&F);
      Function *Callee = CB->getCalledFunction();
      if (Callee && !Callee->isDeclaration()) {
        Calls[&F].push_back(Callee);
        Callers[Callee].push_back(&F);
      } else if (!Callee || !CB->doesNotAccessMemory() ||
                 !CB->hasFnAttr(Attribute::NoCallback) || !CB->doesNotThrow())
        Barriers.insert(&F);
    }
  }
  auto Propagate = [&](Functions &Set) {
    for (unsigned I = 0; I < Set.size(); ++I)
      for (Function *F : Callers[Set[I]])
        Set.insert(F);
  };
  Propagate(Barriers);

  GlobalVariable *Selected = nullptr;
  Functions SelectedTouched;
  unsigned BestScore = 0;
  for (GlobalVariable &G : M.globals()) {
    if (!G.hasLocalLinkage() || G.isConstant() || G.isThreadLocal() ||
        G.getAddressSpace() || !G.getValueType()->isIntegerTy(16))
      continue;
    Functions Touched;
    unsigned Loads = 0, Stores = 0, Score = 0;
    bool Valid = true;
    for (User *U : G.users()) {
      auto *I = dyn_cast<Instruction>(U);
      auto *L = dyn_cast<LoadInst>(U);
      auto *S = dyn_cast<StoreInst>(U);
      if (!I || (!L && !S) ||
          (L && (!L->isSimple() || L->getType() != G.getValueType())) ||
          (S && (!S->isSimple() || S->getPointerOperand() != &G ||
                 S->getValueOperand()->getType() != G.getValueType()))) {
        Valid = false;
        break;
      }
      Touched.insert(I->getFunction());
      /* Keep byte arithmetic out of the state convention until its access
         costs can be modeled across the whole call chain. Otherwise one
         unsupported update can spill the carried word in every caller. */
      if (S && eligible(*I->getFunction()) &&
          I->getFunction()->hasLocalLinkage() &&
          !isBankUpdate(S->getValueOperand(), G)) {
        Valid = false;
        break;
      }
      Loads += L != nullptr;
      Stores += S != nullptr;
    }
    if (!Valid || !Loads || !Stores)
      continue;
    Propagate(Touched);
    for (Function *F : Touched)
      if (eligible(*F) && F->hasLocalLinkage() &&
          F->getCallingConv() == CallingConv::Fast &&
          F->getReturnType()->isVoidTy())
        Score += F->getNumUses();
    // Amortize the entry/exit transfers over a repeatedly called helper chain.
    if (Score >= 4 && Score > BestScore) {
      Selected = &G;
      BestScore = Score;
      SelectedTouched = Touched;
    }
  }
  if (!Selected)
    return false;
  auto &G = *Selected;
  Type *Ty = G.getValueType();
  Functions Candidates, Roots;
  for (Function *F : SelectedTouched) {
    bool Safe = eligible(*F) && !Unsupported.contains(F) &&
                F->getReturnType()->isVoidTy();
    for (Instruction &I : instructions(F))
      if (auto *CB = dyn_cast<CallBase>(&I))
        Safe &= !CB->isInlineAsm();
    if (!Safe)
      continue;
    if (F->hasLocalLinkage() && F->getCallingConv() == CallingConv::Fast) {
      Safe = llvm::all_of(F->users(), [&](User *U) {
        auto *C = dyn_cast<CallInst>(U);
        return C && C->getCalledFunction() == F && !C->isMustTailCall() &&
               !C->hasOperandBundles() && eligible(*C->getFunction());
      });
      if (Safe && llvm::none_of(F->args(), [](Argument &A) {
            return A.hasInRegAttr() || A.hasAttribute(Attribute::StructRet);
          }))
        Candidates.insert(F);
    } else
      Roots.insert(F);
  }
  bool Changed;
  do {
    Changed = false;
    for (Function *F :
         SmallVector<Function *>(Candidates.begin(), Candidates.end()))
      if (llvm::any_of(Calls[F], [&](Function *C) {
            return SelectedTouched.contains(C) && !Candidates.contains(C);
          })) {
        Candidates.remove(F);
        Changed = true;
      }
  } while (Changed);
  if (Candidates.empty())
    return false;
  for (Function *F : SmallVector<Function *>(Roots.begin(), Roots.end()))
    if (llvm::any_of(Calls[F], [&](Function *C) {
          return SelectedTouched.contains(C) && !Candidates.contains(C);
        }))
      Roots.remove(F);
  Functions Cached = Candidates;
  Cached.insert(Roots.begin(), Roots.end());
  Functions Protected = Cached;
  for (unsigned I = 0; I < Protected.size(); ++I)
    for (Function *C : Calls[Protected[I]]) {
      // A foreign subtarget or escaping helper cannot promise the state bank.
      if (!eligible(*C) || !C->hasLocalLinkage())
        return false;
      Protected.insert(C);
    }

  Functions Versions;
  for (Function *F : Candidates)
    if (Calls[F].empty() && F->getInstructionCount() <= 32 &&
        llvm::any_of(G.users(),
                     [&](User *U) {
                       return cast<Instruction>(U)->getFunction() == F;
                     }) &&
        llvm::any_of(F->users(), [&](User *U) {
          return !Cached.contains(cast<Instruction>(U)->getFunction());
        }))
      Versions.insert(F);

  SmallVector<ConstantInt *, 2> Addresses;
  if (CacheBankAddresses && BankAddressCount) {
    MapVector<ConstantInt *, unsigned> Counts, ByteCounts;
    for (Function *F : Candidates)
      for (Instruction &I : instructions(F))
        if (auto *A = absoluteByteAddress(I)) {
          unsigned Weight = F->getNumUses();
          ByteCounts[A] += Weight;
          Counts[A] +=
              Weight * (isa<LoadInst>(I) ? 4
                        : isa<ConstantInt>(cast<StoreInst>(I).getValueOperand())
                            ? 2
                            : 0);
        }
    ConstantInt *HL = nullptr;
    unsigned HLScore = 7;
    for (auto [A, U] : Counts)
      if (U > HLScore) {
        HL = A;
        HLScore = U;
      }
    if (HL) {
      Addresses = {HL};
      ConstantInt *DE = nullptr;
      unsigned DEScore = 7;
      for (auto [A, U] : ByteCounts)
        if (A != HL && U > DEScore) {
          DE = A;
          DEScore = U;
        }
      if (DE && BankAddressCount > 1)
        Addresses.push_back(DE);
    }
  }
  std::string AddressText;
  for (auto *A : Addresses) {
    if (!AddressText.empty())
      AddressText += ',';
    AddressText += std::to_string(A->getZExtValue());
  }

  DenseMap<Function *, Function *> Replacements;
  DenseMap<Function *, AllocaInst *> Slots;
  DenseMap<Function *, SmallVector<Value *, 2>> AddressValues;
  for (Function *F : Candidates) {
    SmallVector<Type *> Params(F->getFunctionType()->params());
    Params.push_back(Ty);
    Params.append(Addresses.size(), Ty);
    auto *N =
        Function::Create(FunctionType::get(Ty, Params, false), F->getLinkage(),
                         F->getAddressSpace(), F->getName() + ".state", &M);
    N->copyAttributesFrom(F);
    ValueToValueMapTy Map;
    auto Dest = N->arg_begin();
    for (Argument &A : F->args()) {
      Dest->setName(A.getName());
      Map[&A] = &*Dest++;
    }
    Dest->setName("cached.state");
    SmallVector<ReturnInst *, 8> Returns;
    CloneFunctionInto(N, F, Map, CloneFunctionChangeType::LocalChangesOnly,
                      Returns);
    if (!Addresses.empty())
      N->setCallingConv(CallingConv::Z80_BankState);
    N->addRetAttr(Attribute::InReg);
    for (unsigned I = F->arg_size(); I != Params.size(); ++I)
      N->addParamAttr(I, Attribute::InReg);
    Replacements[F] = N;
    if (!Versions.contains(F))
      N->takeName(F);
  }
  Functions Bodies;
  for (Function *F : Cached) {
    Function *N = Replacements.lookup(F);
    if (!N)
      N = F;
    Bodies.insert(N);
    IRBuilder<> B(&*N->getEntryBlock().getFirstInsertionPt());
    auto *Slot = B.CreateAlloca(Ty, nullptr, "cached.slot");
    Slots[N] = Slot;
    // Rewrite the original accesses before creating synchronization accesses.
    for (Instruction &I : instructions(N))
      for (Use &U : I.operands())
        if (U == &G)
          U = Slot;
    Value *Initial = Candidates.contains(F)
                         ? static_cast<Value *>(N->getArg(F->arg_size()))
                         : B.CreateLoad(Ty, &G, "cached.entry");
    B.CreateStore(Initial, Slot);
    for (BasicBlock &BB : *N)
      if (auto *R = dyn_cast<ReturnInst>(BB.getTerminator())) {
        IRBuilder<> Exit(R);
        Value *Result = Exit.CreateLoad(Ty, Slot);
        if (Candidates.contains(F)) {
          Exit.CreateRet(Result);
          R->eraseFromParent();
        } else
          Exit.CreateStore(Result, &G);
      }
    N->addFnAttr("z80-state-bank", Addresses.empty() ? "hl" : "bc");
    if (!Addresses.empty()) {
      N->addFnAttr("z80-bank-addresses", AddressText);
      auto &Values = AddressValues[N];
      for (unsigned I = 0; I != Addresses.size(); ++I)
        Values.push_back(
            Candidates.contains(F)
                ? static_cast<Value *>(N->getArg(F->arg_size() + 1 + I))
                : Addresses[I]);
      /* Leaf bodies can consume the resident addresses directly. In callers,
         keep literal addresses available to instruction selection; replacing
         them would tie arbitrary local address uses to the forwarded values. */
      bool Leaf = llvm::none_of(instructions(N), [](const Instruction &I) {
        return isa<CallBase>(I);
      });
      if (Candidates.contains(F) && Leaf)
        for (Instruction &I : instructions(N))
          if (auto *A = absoluteByteAddress(I)) {
            auto Found = llvm::find(Addresses, A);
            if (Found != Addresses.end()) {
              IRBuilder<> At(&I);
              I.setOperand(
                  isa<LoadInst>(I) ? 0 : 1,
                  At.CreateIntToPtr(Values[Found - Addresses.begin()],
                                    PointerType::getUnqual(M.getContext())));
            }
          }
    }
    N->setMemoryEffects(MemoryEffects::unknown());
  }
  for (Function *F : Protected)
    if (!Cached.contains(F)) {
      F->addFnAttr("z80-preserve-state-bank", Addresses.empty() ? "hl" : "bc");
      if (!Addresses.empty())
        F->addFnAttr("z80-bank-addresses", AddressText);
    }

  for (Function &F : M) {
    if (Candidates.contains(&F))
      continue;
    auto *Slot = Slots.lookup(&F);
    for (Instruction &I : make_early_inc_range(instructions(F))) {
      auto *C = dyn_cast<CallInst>(&I);
      if (!C)
        continue;
      Function *Callee = C->getCalledFunction();
      Function *Replacement = Replacements.lookup(Callee);
      IRBuilder<> B(C);
      if (Replacement && (Slot || !Versions.contains(Callee))) {
        Value *Address = Slot ? static_cast<Value *>(Slot) : &G;
        SmallVector<Value *> Args(C->args());
        Args.push_back(B.CreateLoad(Ty, Address));
        for (unsigned I = 0; I != Addresses.size(); ++I)
          Args.push_back(Slot ? AddressValues[&F][I] : Addresses[I]);
        auto *N = B.CreateCall(Replacement, Args);
        N->setCallingConv(Replacement->getCallingConv());
        N->setAttributes(C->getAttributes());
        N->addRetAttr(Attribute::InReg);
        for (unsigned I = C->arg_size(); I != Args.size(); ++I)
          N->addParamAttr(I, Attribute::InReg);
        N->setMemoryEffects(MemoryEffects::unknown());
        N->setDebugLoc(C->getDebugLoc());
        N->setTailCallKind(C->getTailCallKind());
        B.CreateStore(N, Address);
        C->eraseFromParent();
      } else if (Slot && (!Callee || Barriers.contains(Callee) ||
                          (Callee->isDeclaration() &&
                           (!C->doesNotAccessMemory() ||
                            !C->hasFnAttr(Attribute::NoCallback) ||
                            !C->doesNotThrow())))) {
        B.CreateStore(B.CreateLoad(Ty, Slot), &G);
        B.SetInsertPoint(C->getNextNode());
        B.CreateStore(B.CreateLoad(Ty, &G), Slot);
      }
    }
  }
  for (Function *F : Candidates)
    if (!Versions.contains(F))
      F->dropAllReferences();
  for (Function *F : Candidates)
    if (!Versions.contains(F))
      F->eraseFromParent();
  for (Function *F : Bodies) {
    DominatorTree DT(*F);
    PromoteMemToReg({Slots.lookup(F)}, DT);
  }
  /* Specialization can leave the memory-based leaf with a single caller.
     Fold that remaining call into its caller after the banked calls have
     moved to their new version. This also exposes accesses between inline
     assembly memory barriers without carrying cached state across them. */
  for (Function *F : Versions) {
    if (!F->hasOneUse() || F->hasFnAttribute(Attribute::NoInline))
      continue;
    auto *C = dyn_cast<CallInst>(*F->user_begin());
    if (!C || C->getCalledFunction() != F ||
        C->hasFnAttr(Attribute::NoInline) || C->getFunction()->hasOptNone())
      continue;
    InlineFunctionInfo IFI;
    if (InlineFunction(*C, IFI).isSuccess() && F->use_empty())
      F->eraseFromParent();
  }
  specializeAddressRegions(M);
  return true;
}

ModulePass *llvm::createZ80StatePromotion(const Z80TargetMachine &TM) {
  return new Z80StatePromotion(TM);
}
