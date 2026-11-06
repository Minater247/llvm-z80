//===- Z80PrepareAddresses.cpp - Share adjacent addresses -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "Z80Subtarget.h"
#include "Z80TargetMachine.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"

using namespace llvm;

static cl::opt<bool>
    ShareAdjacentAddresses("z80-share-adjacent-addresses", cl::Hidden,
                           cl::init(true),
                           cl::desc("Share repeated adjacent byte addresses"));

namespace {
class Z80PrepareAddresses : public FunctionPass {
  const Z80TargetMachine &TM;

public:
  static char ID;
  explicit Z80PrepareAddresses(const Z80TargetMachine &TM)
      : FunctionPass(ID), TM(TM) {}
  StringRef getPassName() const override { return "Z80 adjacent addresses"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }
  bool runOnFunction(Function &F) override;
};
} // namespace

/// Express repeated neighboring byte addresses relative to one live base.
/// Disjoint constant bits prove that their difference is a fixed displacement.
/// The base must dominate its neighbor, whose uses are byte loads and stores.
bool Z80PrepareAddresses::runOnFunction(Function &F) {
  const auto &ST = *TM.getSubtargetImpl(F);
  if (!ShareAdjacentAddresses || skipFunction(F) || !ST.is16Bit() ||
      ST.hasEZ80Ops())
    return false;
  struct Address {
    IntToPtrInst *Pointer;
    Value *Base;
    APInt Offset;
  };
  SmallVector<Address, 16> Addresses;
  for (Instruction &I : instructions(F)) {
    auto *P = dyn_cast<IntToPtrInst>(&I);
    if (!P || P->getAddressSpace() ||
        !P->getOperand(0)->getType()->isIntegerTy(16) || !P->hasNUsesOrMore(2))
      continue;
    auto *B = dyn_cast<BinaryOperator>(P->getOperand(0));
    if (!B || B->getOpcode() != Instruction::Or)
      continue;
    auto *C = dyn_cast<ConstantInt>(B->getOperand(1));
    if (!C || !llvm::all_of(P->users(), [P](User *U) {
          if (auto *L = dyn_cast<LoadInst>(U))
            return L->getPointerOperand() == P && L->getType()->isIntegerTy(8);
          if (auto *S = dyn_cast<StoreInst>(U))
            return S->getPointerOperand() == P &&
                   S->getValueOperand()->getType()->isIntegerTy(8);
          return false;
        }))
      continue;
    KnownBits Bits = computeKnownBits(B->getOperand(0), F.getDataLayout());
    if ((Bits.Zero & C->getValue()) != C->getValue())
      continue;
    if (Addresses.size() == 64)
      return false;
    Addresses.push_back({P, B->getOperand(0), C->getValue()});
  }
  if (Addresses.size() < 2)
    return false;
  DominatorTree DT(F);
  SmallPtrSet<IntToPtrInst *, 8> Replaced;
  bool Changed = false;
  for (const auto &A : Addresses)
    for (const auto &B : Addresses) {
      APInt Difference = A.Offset - B.Offset;
      if (A.Base != B.Base ||
          (!Difference.isOne() && !Difference.isAllOnes()) ||
          Replaced.contains(A.Pointer) || Replaced.contains(B.Pointer) ||
          !DT.dominates(B.Pointer, A.Pointer))
        continue;
      IRBuilder<> At(A.Pointer);
      Value *P = At.CreateGEP(At.getInt8Ty(), B.Pointer,
                              ConstantInt::get(F.getContext(), Difference),
                              "adjacent.address");
      A.Pointer->replaceAllUsesWith(P);
      Replaced.insert(A.Pointer);
      Changed = true;
      break;
    }
  for (auto *P : Replaced)
    P->eraseFromParent();
  return Changed;
}

char Z80PrepareAddresses::ID;
FunctionPass *llvm::createZ80PrepareAddresses(const Z80TargetMachine &TM) {
  return new Z80PrepareAddresses(TM);
}
