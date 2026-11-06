//===- Z80PrivateMemory.cpp - Cache private byte state ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <memory>

using namespace llvm;

static cl::opt<bool> CachePrivateMemory(
    "z80-cache-private-memory", cl::Hidden, cl::init(true),
    cl::desc("Keep private byte state across unrelated calls"));

static cl::opt<bool> CachePrivateLeafLoops(
    "z80-cache-private-leaf-loops", cl::Hidden, cl::init(true),
    cl::desc("Keep private byte state in unconditional leaf loops"));

namespace {
class Z80PrivateMemory : public ModulePass {
public:
  static char ID;
  Z80PrivateMemory() : ModulePass(ID) {}
  StringRef getPassName() const override {
    return "Z80 private memory caching";
  }
  bool runOnModule(Module &M) override;
};
} // namespace

/// Keep nonescaping byte globals in SSA within loops. Known direct calls
/// synchronize only the globals they read or write; unknown calls synchronize
/// every cached value. Volatile and atomic objects retain their accesses.
bool Z80PrivateMemory::runOnModule(Module &M) {
  if (!CachePrivateMemory)
    return false;
  DenseMap<Function *, SmallVector<CallInst *, 8>> Calls;
  SmallPtrSet<Function *, 16> Unsupported;
  for (Function &F : M)
    for (Instruction &I : instructions(F)) {
      if (isa<FenceInst, AtomicRMWInst, AtomicCmpXchgInst>(I) ||
          (isa<LoadInst>(I) && cast<LoadInst>(I).isAtomic()) ||
          (isa<StoreInst>(I) && cast<StoreInst>(I).isAtomic()))
        Unsupported.insert(&F);
      if (auto *CB = dyn_cast<CallBase>(&I)) {
        if (!isa<CallInst>(CB) || CB->isMustTailCall() ||
            CB->hasOperandBundles() || !CB->doesNotThrow())
          Unsupported.insert(&F);
        else
          Calls[&F].push_back(cast<CallInst>(CB));
      }
    }
  bool Changed = false;
  DenseMap<Function *, std::unique_ptr<DominatorTree>> DTs;
  DenseMap<Function *, std::unique_ptr<LoopInfo>> Loops;
  DenseMap<Function *, std::unique_ptr<BranchProbabilityInfo>> Probabilities;
  DenseMap<Function *, std::unique_ptr<BlockFrequencyInfo>> Frequencies;
  for (GlobalVariable &G : M.globals()) {
    if (!G.hasLocalLinkage() || G.isConstant() || G.isThreadLocal() ||
        G.getAddressSpace() || !G.getValueType()->isIntegerTy(8))
      continue;
    enum { Read = 1, Write = 2 };
    DenseMap<Function *, unsigned> Effects;
    DenseMap<Function *, SmallVector<Instruction *, 8>> Accesses;
    bool Valid = true;
    for (User *U : G.users()) {
      auto *L = dyn_cast<LoadInst>(U);
      auto *S = dyn_cast<StoreInst>(U);
      if ((!L && !S) ||
          (L && (!L->isSimple() || L->getType() != G.getValueType())) ||
          (S && (!S->isSimple() || S->getPointerOperand() != &G ||
                 S->getValueOperand()->getType() != G.getValueType()))) {
        Valid = false;
        break;
      }
      auto *I = cast<Instruction>(U);
      Effects[I->getFunction()] |= L ? Read : Write;
      Accesses[I->getFunction()].push_back(I);
    }
    if (!Valid || Accesses.empty())
      continue;
    auto CallEffects = [&](CallInst *C) -> unsigned {
      Function *F = C->getCalledFunction();
      if (F && !F->isDeclaration() && !F->isInterposable())
        return Effects.lookup(F);
      if (C->doesNotAccessMemory() && C->hasFnAttr(Attribute::NoCallback))
        return 0;
      return Read | Write;
    };
    bool More;
    do {
      More = false;
      for (Function &F : M) {
        unsigned E = Effects.lookup(&F);
        if (Unsupported.contains(&F))
          E |= Read | Write;
        for (CallInst *C : Calls[&F])
          E |= CallEffects(C);
        if (E != Effects.lookup(&F)) {
          Effects[&F] = E;
          More = true;
        }
      }
    } while (More);
    for (Function &F : M) {
      auto It = Accesses.find(&F);
      if (It == Accesses.end() || It->second.size() < 2 ||
          Unsupported.contains(&F) || F.hasOptNone() ||
          F.hasFnAttribute(Attribute::Naked) ||
          llvm::any_of(Calls[&F], [](CallInst *C) {
            if (C->doesNotAccessMemory() && C->hasFnAttr(Attribute::NoCallback))
              return false;
            auto *Callee = C->getCalledFunction();
            return !Callee || Callee->isDeclaration() ||
                   Callee->isInterposable();
          }))
        continue;
      auto &DT = DTs[&F];
      auto &LI = Loops[&F];
      if (!DT) {
        DT = std::make_unique<DominatorTree>(F);
        LI = std::make_unique<LoopInfo>(*DT);
      }
      /* A nonescaping byte cannot change within a read-only leaf. Late
         specialization can expose repeated reads after the ordinary scalar
         optimizations have run; share their value at their common dominator. */
      if (Effects.lookup(&F) == Read &&
          llvm::all_of(Calls[&F], [](CallInst *C) {
            return C->doesNotAccessMemory() &&
                   C->hasFnAttr(Attribute::NoCallback);
          })) {
        BasicBlock *Block = It->second.front()->getParent();
        for (Instruction *I : drop_begin(It->second)) {
          Block = DT->findNearestCommonDominator(Block, I->getParent());
          if (!Block)
            break;
        }
        if (Block) {
          IRBuilder<> B(&*Block->getFirstInsertionPt());
          auto *Value = B.CreateLoad(G.getValueType(), &G, "private.value");
          for (Instruction *I : It->second) {
            I->replaceAllUsesWith(Value);
            I->eraseFromParent();
          }
          Changed = true;
          continue;
        }
      }
      BasicBlock *Header = It->second.front()->getParent();
      Loop *HeaderLoop = LI->getLoopFor(Header);
      /* A leaf cursor loop amortizes its entry load and exit store even
         when its body contains only one load/store pair. Requiring the
         header on every returning path avoids charging empty-loop exits
         for this unconditional cache. */
      bool LeafLoop = CachePrivateLeafLoops && Calls[&F].empty() && HeaderLoop &&
                      HeaderLoop->getHeader() == Header &&
                      llvm::all_of(It->second, [&](Instruction *I) {
                        return I->getParent() == Header;
                      }) &&
                      llvm::any_of(It->second, [](Instruction *I) {
                        return isa<LoadInst>(I);
                      }) &&
                      llvm::any_of(It->second, [](Instruction *I) {
                        return isa<StoreInst>(I);
                      }) &&
                      llvm::all_of(F, [&](BasicBlock &BB) {
                        return !isa<ReturnInst>(BB.getTerminator()) ||
                               DT->dominates(Header, &BB);
                      });
      if (!LeafLoop && llvm::none_of(It->second, [&](Instruction *I) {
            Loop *L = LI->getLoopFor(I->getParent());
            return L && llvm::any_of(Calls[&F], [&](CallInst *C) {
                     return L->contains(C->getParent()) && !CallEffects(C);
                   });
          }))
        continue;
      unsigned AccessCount = 0, SyncCount = 0;
      for (Instruction *I : It->second)
        AccessCount += LI->getLoopFor(I->getParent()) != nullptr;
      for (CallInst *C : Calls[&F])
        if (LI->getLoopFor(C->getParent())) {
          unsigned E = CallEffects(C);
          SyncCount += !!E + !!(E & Write);
        }
      /* An observed global has to be published at every observing call.
         Retaining infrequently changed configuration would add stores at
         those calls instead of removing accesses from the loop. */
      if (!LeafLoop && AccessCount <= SyncCount * 2 + 2) {
        /* A dispatcher can perform many synchronizing calls in one arm and
           keep state locally in the others. Count their execution frequency
           before rejecting the cache: adding a helper to one arm must not
           force every iteration through memory. Build this analysis only
           for candidates the inexpensive count would otherwise reject. */
        auto &BFI = Frequencies[&F];
        if (!BFI) {
          auto &BPI = Probabilities[&F];
          BPI = std::make_unique<BranchProbabilityInfo>(F, *LI);
          BFI = std::make_unique<BlockFrequencyInfo>(F, *BPI, *LI);
        }
        auto Frequency = [&](BasicBlock *BB) -> double {
          return BFI->getBlockFreq(BB).getFrequency();
        };
        double AccessCost = 0;
        double SyncCost = Frequency(&F.getEntryBlock());
        bool Written = Effects.lookup(&F) & Write;
        for (Instruction *I : It->second)
          AccessCost += Frequency(I->getParent());
        for (CallInst *C : Calls[&F]) {
          unsigned E = CallEffects(C);
          if (E)
            SyncCost += Frequency(C->getParent()) *
                        (unsigned(Written) + bool(E & Write));
        }
        if (Written)
          for (BasicBlock &BB : F)
            if (isa<ReturnInst>(BB.getTerminator()))
              SyncCost += Frequency(&BB);
        if (AccessCost <= SyncCost)
          continue;
      }
      /* Begin caching where all direct accesses are reachable. Keeping the
         cache inside this dominated region leaves early exits and unrelated
         calls outside it untouched. Start outside loops so a backedge keeps
         the current SSA value instead of reloading unpublished memory. */
      BasicBlock *Root = It->second.front()->getParent();
      for (Instruction *I : It->second) {
        Root = DT->findNearestCommonDominator(Root, I->getParent());
        if (!Root) {
          Root = &F.getEntryBlock();
          break;
        }
      }
      while (LI->getLoopFor(Root))
        Root = DT->getNode(Root)->getIDom()->getBlock();
      SmallPtrSet<BasicBlock *, 32> Region;
      for (BasicBlock &BB : F)
        if (DT->dominates(Root, &BB))
          Region.insert(&BB);
      IRBuilder<> B(&*F.getEntryBlock().getFirstInsertionPt());
      bool Written = Effects.lookup(&F) & Write;
      SmallPtrSet<Instruction *, 16> Publish;
      if (Written) {
        /* A synchronizing call leaves the cache equal to memory. Another
           observer in the same block needs a store only after a local
           write. Keeping this distinction also makes unused reloads dead
           when several observers execute consecutively. Block entries are
           conservative because a predecessor may have changed the cache. */
        SmallPtrSet<Instruction *, 16> Writes;
        for (Instruction *I : It->second)
          if (isa<StoreInst>(I))
            Writes.insert(I);
        for (BasicBlock &BB : F) {
          if (!Region.contains(&BB))
            continue;
          bool Dirty = &BB != Root;
          for (Instruction &I : BB) {
            if (Writes.contains(&I))
              Dirty = true;
            else if (auto *C = dyn_cast<CallInst>(&I)) {
              if (CallEffects(C)) {
                if (Dirty)
                  Publish.insert(C);
                Dirty = false;
              }
            } else if (I.isTerminator() && Dirty &&
                       (isa<ReturnInst>(I) ||
                        llvm::any_of(successors(&BB), [&](BasicBlock *Succ) {
                          return !Region.contains(Succ);
                        }))) {
              Publish.insert(&I);
            }
          }
        }
      }
      auto *Slot = B.CreateAlloca(G.getValueType(), nullptr, "private.slot");
      B.SetInsertPoint(Root, Root == &F.getEntryBlock()
                                 ? std::next(Slot->getIterator())
                                 : Root->getFirstInsertionPt());
      B.CreateStore(B.CreateLoad(G.getValueType(), &G), Slot);
      for (Instruction *I : It->second)
        I->setOperand(isa<LoadInst>(I) ? 0 : 1, Slot);
      for (CallInst *C : Calls[&F]) {
        if (!Region.contains(C->getParent()))
          continue;
        unsigned E = CallEffects(C);
        if (!E)
          continue;
        B.SetInsertPoint(C);
        if (Publish.contains(C))
          B.CreateStore(B.CreateLoad(G.getValueType(), Slot), &G);
        if (E & Write) {
          B.SetInsertPoint(C->getNextNode());
          B.CreateStore(B.CreateLoad(G.getValueType(), &G), Slot);
        }
      }
      if (Written)
        for (BasicBlock &BB : F)
          if (auto *T = BB.getTerminator(); Publish.contains(T)) {
            B.SetInsertPoint(T);
            B.CreateStore(B.CreateLoad(G.getValueType(), Slot), &G);
          }
      PromoteMemToReg({Slot}, *DT);
      F.setMemoryEffects(MemoryEffects::unknown());
      for (User *U : F.users())
        if (auto *C = dyn_cast<CallBase>(U))
          C->setMemoryEffects(MemoryEffects::unknown());
      Changed = true;
    }
  }
  return Changed;
}

char Z80PrivateMemory::ID;
ModulePass *llvm::createZ80PrivateMemory() { return new Z80PrivateMemory(); }
