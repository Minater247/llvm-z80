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

namespace {
using Functions = SmallSetVector<Function *, 16>;

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

/// Carry one private word through a closed set of direct internal calls.
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

  DenseMap<Function *, Function *> Replacements;
  DenseMap<Function *, AllocaInst *> Slots;
  for (Function *F : Candidates) {
    SmallVector<Type *> Params(F->getFunctionType()->params());
    Params.push_back(Ty);
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
    N->addRetAttr(Attribute::InReg);
    N->addParamAttr(Params.size() - 1, Attribute::InReg);
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
                         ? static_cast<Value *>(N->getArg(N->arg_size() - 1))
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
    N->addFnAttr("z80-state-bank", "hl");
    N->setMemoryEffects(MemoryEffects::unknown());
  }
  for (Function *F : Protected)
    if (!Cached.contains(F))
      F->addFnAttr("z80-preserve-state-bank", "hl");

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
        auto *N = B.CreateCall(Replacement, Args);
        N->setCallingConv(C->getCallingConv());
        N->setAttributes(C->getAttributes());
        N->addRetAttr(Attribute::InReg);
        N->addParamAttr(Args.size() - 1, Attribute::InReg);
        N->setMemoryEffects(MemoryEffects::unknown());
        N->setDebugLoc(C->getDebugLoc());
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
  return true;
}

ModulePass *llvm::createZ80StatePromotion(const Z80TargetMachine &TM) {
  return new Z80StatePromotion(TM);
}
