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
      if (llvm::none_of(It->second, [&](Instruction *I) {
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
      if (AccessCount <= SyncCount * 2 + 2)
        continue;
      IRBuilder<> B(&*F.getEntryBlock().getFirstInsertionPt());
      auto *Slot = B.CreateAlloca(G.getValueType(), nullptr, "private.slot");
      B.CreateStore(B.CreateLoad(G.getValueType(), &G), Slot);
      bool Written = Effects.lookup(&F) & Write;
      for (Instruction *I : It->second)
        I->setOperand(isa<LoadInst>(I) ? 0 : 1, Slot);
      for (CallInst *C : Calls[&F]) {
        unsigned E = CallEffects(C);
        if (!E)
          continue;
        B.SetInsertPoint(C);
        if (Written)
          B.CreateStore(B.CreateLoad(G.getValueType(), Slot), &G);
        if (E & Write) {
          B.SetInsertPoint(C->getNextNode());
          B.CreateStore(B.CreateLoad(G.getValueType(), &G), Slot);
        }
      }
      if (Written)
        for (BasicBlock &BB : F)
          if (auto *R = dyn_cast<ReturnInst>(BB.getTerminator())) {
            B.SetInsertPoint(R);
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
