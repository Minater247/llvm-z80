//===- Z80PrepareLoops.cpp - Reduce repeated loop arithmetic ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "Z80Subtarget.h"
#include "Z80TargetMachine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/LoopRotationUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

static cl::opt<bool> PrepareLoopMasks(
    "z80-prepare-loop-masks", cl::Hidden, cl::init(true),
    cl::desc("Advance bounded loop masks with single-bit shifts"));

static cl::opt<bool> RotateCountedLoops(
    "z80-rotate-counted-loops", cl::Hidden, cl::init(true),
    cl::desc("Test nonempty countdown loops after their bodies"));

namespace {
class Z80PrepareLoops : public FunctionPass {
  const Z80TargetMachine &TM;

public:
  static char ID;
  explicit Z80PrepareLoops(const Z80TargetMachine &TM)
      : FunctionPass(ID), TM(TM) {}
  StringRef getPassName() const override { return "Z80 loop arithmetic"; }
  bool runOnFunction(Function &F) override;
};
} // namespace

/// Reduce repeated mask construction and countdown-loop control.
/// Proven nonempty countdowns test at the end of each iteration. Bounded shift
/// counts admit incremental masks, using bytes when every mask fits. Empty
/// loops and conditional memory accesses retain their execution conditions.
bool Z80PrepareLoops::runOnFunction(Function &F) {
  const auto &ST = *TM.getSubtargetImpl(F);
  if ((!PrepareLoopMasks && !RotateCountedLoops) || skipFunction(F) ||
      !ST.is16Bit() || ST.hasEZ80Ops())
    return false;
  SmallVector<BinaryOperator *, 8> Shifts;
  for (Instruction &I : instructions(F))
    if (auto *B = dyn_cast<BinaryOperator>(&I))
      if (B->getOpcode() == Instruction::Shl &&
          (B->getType()->isIntegerTy(8) || B->getType()->isIntegerTy(16)) &&
          isa<ConstantInt>(B->getOperand(0)) &&
          !isa<ConstantInt>(B->getOperand(1))) {
        if (Shifts.size() == 32)
          return false;
        Shifts.push_back(B);
      }
  bool MayRotate = RotateCountedLoops && llvm::any_of(F, [](BasicBlock &BB) {
    return !BB.phis().empty() && isa<ICmpInst>(BB.getFirstNonPHIIt()) &&
           std::next(BB.getFirstNonPHIIt()) == BB.getTerminator()->getIterator();
  });
  if ((!PrepareLoopMasks || Shifts.empty()) && !MayRotate)
    return false;
  DominatorTree DT(F);
  LoopInfo LI(DT);
  AssumptionCache AC(F);
  TargetLibraryInfoImpl TLII(TM.getTargetTriple());
  TargetLibraryInfo TLI(TLII, &F);
  ScalarEvolution SE(F, TLI, AC, DT, LI);
  bool Changed = false;
  if (MayRotate) {
    auto TTI = TM.getTargetTransformInfo(F);
    const SimplifyQuery SQ(F.getDataLayout(), &TLI, &DT, &AC);
    unsigned Budget = 32;
    for (Loop *L : LI.getLoopsInPreorder()) {
      if (!Budget--)
        break;
      BasicBlock *Header = L->getHeader();
      auto *Branch = dyn_cast<BranchInst>(Header->getTerminator());
      if (!L->getLoopPreheader() || !L->getLoopLatch() || !Branch ||
          !Branch->isConditional() ||
          std::next(Header->getFirstNonPHIIt()) !=
              Header->getTerminator()->getIterator())
        continue;
      auto *Cmp = dyn_cast<ICmpInst>(Branch->getCondition());
      if (!Cmp || !Cmp->isEquality() || Cmp->getParent() != Header ||
          !Cmp->hasOneUse())
        continue;
      auto *Counter = dyn_cast<PHINode>(Cmp->getOperand(0));
      auto *Zero = dyn_cast<ConstantInt>(Cmp->getOperand(1));
      if (!Counter || Counter->getParent() != Header || !Zero ||
          !Zero->isZero() || Counter->getNumIncomingValues() != 2 ||
          (!Counter->getType()->isIntegerTy(8) &&
           !Counter->getType()->isIntegerTy(16)))
        continue;
      unsigned Body = Cmp->getPredicate() == ICmpInst::ICMP_EQ ? 1 : 0;
      if (!L->contains(Branch->getSuccessor(Body)) ||
          L->contains(Branch->getSuccessor(1 - Body)))
        continue;
      Value *Initial = Counter->getIncomingValueForBlock(L->getLoopPreheader());
      auto *Step = dyn_cast<BinaryOperator>(
          Counter->getIncomingValueForBlock(L->getLoopLatch()));
      auto *Amount = Step ? dyn_cast<ConstantInt>(Step->getOperand(1)) : nullptr;
      if (!Step || Step->getOpcode() != Instruction::Add ||
          Step->getOperand(0) != Counter || !Amount ||
          !Amount->isMinusOne() || !SE.isKnownNonZero(SE.getSCEV(Initial)))
        continue;
      /* The first test is known to enter the body. Rotating this two-
         instruction header removes its repeated jump and exposes the
         decrement flags, without duplicating body work or memory accesses. */
      if (formDedicatedExitBlocks(L, &DT, &LI, nullptr, false)) {
        SE.forgetTopmostLoop(L);
        Changed = true;
      }
      Changed |= LoopRotation(L, &LI, &TTI, &AC, &DT, &SE, nullptr, SQ, true, 2,
                              true);
    }
  }
  if (!PrepareLoopMasks)
    return Changed;
  for (BinaryOperator *Shift : Shifts) {
    const SCEV *Count = SE.getSCEV(Shift->getOperand(1));
    auto *AR = dyn_cast<SCEVAddRecExpr>(Count);
    if (!AR || !AR->isAffine())
      continue;
    auto *Start = dyn_cast<SCEVConstant>(AR->getStart());
    auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
    if (!Start || !Step || !Step->getAPInt().isOne())
      continue;
    const Loop *L = AR->getLoop();
    BasicBlock *Entry = L->getLoopPreheader(), *Latch = L->getLoopLatch();
    if (!Entry || !Latch || !L->contains(Shift))
      continue;
    unsigned Width = Shift->getType()->getIntegerBitWidth();
    APInt Max = SE.getUnsignedRange(Count).getUnsignedMax();
    if (Max.uge(Width) || Start->getAPInt().uge(Width))
      continue;
    /* Short count ranges need few shifts in total. Keeping their mask live
       through the loop body can cost more spills than those shifts save. */
    if (Max.getZExtValue() <= Start->getAPInt().getZExtValue() + 2)
      continue;
    APInt Base = cast<ConstantInt>(Shift->getOperand(0))->getValue();
    if (Base.isZero())
      continue;
    unsigned Bits = Base.getActiveBits() + Max.getZExtValue() <= 8 ? 8 : Width;
    Type *Ty = IntegerType::get(F.getContext(), Bits);
    APInt Initial =
        Base.shl(Start->getAPInt().getZExtValue()).zextOrTrunc(Bits);
    auto *Mask = PHINode::Create(Ty, 2, "loop.mask",
                                L->getHeader()->getFirstNonPHIIt());
    Mask->addIncoming(ConstantInt::get(Ty, Initial), Entry);
    IRBuilder<> Update(Latch->getTerminator());
    Mask->addIncoming(Update.CreateShl(Mask, 1, "loop.mask.next"), Latch);
    IRBuilder<> At(Shift);
    Value *Result = At.CreateZExtOrTrunc(Mask, Shift->getType());
    SE.forgetValue(Shift);
    Shift->replaceAllUsesWith(Result);
    Shift->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

char Z80PrepareLoops::ID;
FunctionPass *llvm::createZ80PrepareLoops(const Z80TargetMachine &TM) {
  return new Z80PrepareLoops(TM);
}
