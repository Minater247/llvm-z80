//===- Z80PrepareDispatch.cpp - Place switch fields ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "Z80Subtarget.h"
#include "Z80TargetMachine.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Local.h"
#include <memory>

using namespace llvm;

static cl::opt<bool> PrepareDispatch(
    "z80-prepare-dispatch", cl::Hidden, cl::init(true),
    cl::desc("Compute shared byte fields in their dispatch handlers"));

namespace {
class Z80PrepareDispatch : public FunctionPass {
  const Z80TargetMachine &TM;

public:
  static char ID;
  explicit Z80PrepareDispatch(const Z80TargetMachine &TM)
      : FunctionPass(ID), TM(TM) {}
  StringRef getPassName() const override {
    return "Z80 dispatch field placement";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }
  bool runOnFunction(Function &F) override;
};
} // namespace

/// Compute byte fields in the branch or switch handlers that consume them.
/// Each destination dominates its replaced uses and shares the input byte
/// with the dispatch. Fields contain up to two constant masks or shifts and
/// remain outside loops deeper than their original block.
bool Z80PrepareDispatch::runOnFunction(Function &F) {
  const auto &ST = *TM.getSubtargetImpl(F);
  if (!PrepareDispatch || skipFunction(F) || !ST.is16Bit() || ST.hasEZ80Ops())
    return false;
  std::unique_ptr<DominatorTree> DT;
  std::unique_ptr<LoopInfo> Loops;
  bool Changed = false;
  unsigned RemainingClones = 128;
  for (BasicBlock &BB : F) {
    auto *Switch = dyn_cast<SwitchInst>(BB.getTerminator());
    auto *Branch = dyn_cast<BranchInst>(BB.getTerminator());
    if (!Switch && (!Branch || !Branch->isConditional()))
      continue;
    auto IsField = [&](Value *V) {
      auto *I = dyn_cast<BinaryOperator>(V);
      return I && I->getParent() == &BB && I->getType()->isIntegerTy(8) &&
             isa<ConstantInt>(I->getOperand(1)) &&
             (I->getOpcode() == Instruction::And ||
              I->getOpcode() == Instruction::LShr);
    };
    SmallVector<Instruction *, 8> Fields;
    for (Instruction &I : BB)
      if (IsField(&I))
        Fields.push_back(&I);
    if (Fields.empty())
      continue;
    SmallSetVector<BasicBlock *, 16> Roots;
    if (!DT) {
      DT = std::make_unique<DominatorTree>(F);
      Loops = std::make_unique<LoopInfo>(*DT);
    }
    if (auto *Node = DT->getNode(&BB))
      for (auto *Child : *Node)
        if (Loops->getLoopDepth(Child->getBlock()) <= Loops->getLoopDepth(&BB))
          Roots.insert(Child->getBlock());
    if (Roots.size() < 2 || Roots.size() > 16)
      continue;
    for (Instruction *I : llvm::reverse(Fields)) {
      if (isInstructionTriviallyDead(I)) {
        I->eraseFromParent();
        Changed = true;
        continue;
      }
      SmallVector<Instruction *, 2> Chain{I};
      Value *Input = I->getOperand(0);
      if (IsField(Input)) {
        Chain.push_back(cast<Instruction>(Input));
        Input = Chain.back()->getOperand(0);
      }
      /* Reusing an input already live into the handlers releases the decoded
         field without introducing a new live value across the dispatch. */
      bool InputLive = llvm::any_of(Input->users(), [&](User *U) {
        auto *Use = dyn_cast<Instruction>(U);
        return Use && Use->getParent() != &BB;
      });
      if (!InputLive)
        continue;
      SmallVector<std::pair<Use *, BasicBlock *>, 16> Uses;
      SmallSetVector<BasicBlock *, 8> Destinations;
      bool Valid = true;
      for (Use &U : I->uses()) {
        auto *User = dyn_cast<Instruction>(U.getUser());
        if (User == BB.getTerminator())
          continue;
        if (!User || isa<PHINode>(User) || User->getParent() == &BB) {
          Valid = false;
          break;
        }
        BasicBlock *Root = nullptr;
        for (BasicBlock *R : Roots)
          if (DT->dominates(R, User->getParent())) {
            Root = R;
            break;
          }
        if (!Root) {
          Valid = false;
          break;
        }
        Destinations.insert(Root);
        Uses.push_back({&U, Root});
      }
      unsigned Clones = Destinations.size() * Chain.size();
      if (!Valid || Destinations.empty() || Clones > 16 ||
          Clones > RemainingClones)
        continue;
      /* An input consumed in a destination is already live at the dispatch.
         Sharing it between handlers can release several decoded fields at
         the point where the jump table also needs address registers. */
      if (llvm::none_of(Destinations, [&](BasicBlock *Root) {
            return llvm::any_of(Input->users(), [&](User *U) {
              auto *Use = dyn_cast<Instruction>(U);
              return Use && !isa<PHINode>(Use) &&
                     DT->dominates(Root, Use->getParent());
            });
          }))
        continue;
      RemainingClones -= Clones;
      for (BasicBlock *Root : Destinations) {
        Value *V = Input;
        auto Insert = Root->getFirstInsertionPt();
        for (Instruction *Part : llvm::reverse(Chain)) {
          auto *Clone = Part->clone();
          Clone->setOperand(0, V);
          Clone->setName("dispatch.field");
          Clone->insertBefore(Insert);
          V = Clone;
        }
        for (auto [U, R] : Uses)
          if (R == Root)
            U->set(V);
      }
      Changed = true;
      /* Drop the replaced child before examining its input. A dead mask
         would otherwise pin the shared shift in the original block. */
      if (isInstructionTriviallyDead(I))
        I->eraseFromParent();
    }
  }
  return Changed;
}

char Z80PrepareDispatch::ID;
FunctionPass *llvm::createZ80PrepareDispatch(const Z80TargetMachine &TM) {
  return new Z80PrepareDispatch(TM);
}
