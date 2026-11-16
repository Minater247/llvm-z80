//===- Z80PrepareLoops.cpp - Reduce repeated loop arithmetic
//----------------===//
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

static cl::opt<bool> RebaseForwardCursors(
    "z80-rebase-forward-cursors", cl::Hidden, cl::init(true),
    cl::desc("Keep forward byte-stream induction at its first address"));

/// Base a complete forward byte stream on its first accessed address.
/// Accept word induction over groups of 4 to 32 bytes loaded in address order.
/// Rebase all relative uses modulo the word width,
/// preserving early-exit addresses and the original memory-operation order.
static bool rebaseForwardCursors(LoopInfo &LI, DominatorTree &DT) {
  bool Changed = false;
  unsigned Budget = 32;
  for (Loop *L : LI.getLoopsInPreorder()) {
    if (!Budget--)
      break;
    auto *Entry = L->getLoopPreheader(), *Latch = L->getLoopLatch();
    if (!Entry || !Latch || L->getNumBlocks() > 4)
      continue;
    for (auto &Phi : L->getHeader()->phis()) {
      if (!Phi.getType()->isIntegerTy(16) || Phi.getNumIncomingValues() != 2)
        continue;
      auto *Step =
          dyn_cast<BinaryOperator>(Phi.getIncomingValueForBlock(Latch));
      auto *Amount =
          Step ? dyn_cast<ConstantInt>(Step->getOperand(1)) : nullptr;
      if (!Step || Step->getOpcode() != Instruction::Add ||
          Step->getOperand(0) != &Phi || !Amount || !Step->hasOneUse())
        continue;
      int64_t Stride = Amount->getSExtValue();
      if (Stride < 4 || Stride > 32)
        continue;
      SmallVector<BinaryOperator *, 16> Offsets;
      SmallVector<IntToPtrInst *, 4> LastPointers;
      SmallVector<LoadInst *, 64> Loads(2 * Stride - 1, nullptr);
      SmallPtrSet<IntToPtrInst *, 16> SeenPointers;
      bool Legal = true;
      unsigned Uses = 0;
      for (User *U : Phi.users()) {
        if (U == Step)
          continue;
        if (++Uses > 64) {
          Legal = false;
          break;
        }
        Value *Address = U;
        int64_t Offset = 0;
        if (auto *Add = dyn_cast<BinaryOperator>(U)) {
          auto *C = dyn_cast<ConstantInt>(Add->getOperand(1));
          if (Add->getOpcode() != Instruction::Add ||
              Add->getOperand(0) != &Phi || !C || C->getSExtValue() >= Stride ||
              C->getSExtValue() <= -Stride) {
            Legal = false;
            break;
          }
          Offset = C->getSExtValue();
          Offsets.push_back(Add);
        } else if (auto *P = dyn_cast<IntToPtrInst>(U)) {
          LastPointers.push_back(P);
          Address = &Phi;
        } else {
          Legal = false;
          break;
        }
        for (User *AU : Address->users()) {
          auto *P = dyn_cast<IntToPtrInst>(AU);
          if (!P || !SeenPointers.insert(P).second)
            continue;
          SmallVector<std::pair<User *, int64_t>, 16> MemoryUsers;
          for (User *PU : P->users()) {
            if (auto *GEP = dyn_cast<GetElementPtrInst>(PU)) {
              auto *C = GEP->getNumIndices() == 1
                            ? dyn_cast<ConstantInt>(GEP->getOperand(1))
                            : nullptr;
              if (!GEP->getSourceElementType()->isIntegerTy(8) || !C ||
                  !C->getValue().isSignedIntN(16)) {
                Legal = false;
                break;
              }
              for (User *GU : GEP->users()) {
                if (MemoryUsers.size() == 64) {
                  Legal = false;
                  break;
                }
                MemoryUsers.emplace_back(GU, Offset + C->getSExtValue());
              }
            } else {
              MemoryUsers.emplace_back(PU, Offset);
            }
            if (!Legal || MemoryUsers.size() == 64) {
              Legal = false;
              break;
            }
          }
          if (!Legal)
            break;
          for (auto [PU, At] : MemoryUsers) {
            auto *Load = dyn_cast<LoadInst>(PU);
            if (!Load || !Load->getType()->isIntegerTy(8) ||
                !L->contains(Load) || P->getAddressSpace() || At >= Stride ||
                At <= -Stride ||
                (Loads[At + Stride - 1] && Loads[At + Stride - 1] != Load)) {
              Legal = false;
              break;
            }
            Loads[At + Stride - 1] = Load;
          }
        }
        if (!Legal)
          break;
      }
      if (!Legal)
        continue;
      auto First = llvm::find_if(Loads, [](auto *Load) { return Load; });
      int64_t FirstOffset = First - Loads.begin() - (Stride - 1);
      Loads.erase(Loads.begin(), First);
      if (Loads.size() < unsigned(Stride) ||
          llvm::any_of(ArrayRef(Loads).take_front(Stride),
                       [](auto *Load) { return !Load; }) ||
          llvm::any_of(ArrayRef(Loads).drop_front(Stride),
                       [](auto *Load) { return Load; }))
        continue;
      Loads.resize(Stride);
      for (unsigned I = 1; I < Loads.size(); ++I)
        Legal &= DT.dominates(Loads[I - 1], Loads[I]);
      if (!Legal || !DT.dominates(Loads.back(), Latch->getTerminator()))
        continue;
      /* LSR's signed-offset preference can retain the last address through
         the whole group. Starting at the first byte lets unit increments
         consume the old cursor instead of reconstructing it on each trip. */
      IRBuilder<> AtEntry(Entry->getTerminator());
      Value *Initial = Phi.getIncomingValueForBlock(Entry);
      Phi.setIncomingValueForBlock(
          Entry, AtEntry.CreateAdd(Initial, AtEntry.getInt16(FirstOffset),
                                   "stream.start"));
      SmallVector<Value *, 33> Addresses;
      Addresses.push_back(&Phi);
      for (auto *Load : Loads) {
        IRBuilder<> At(Load);
        Load->setOperand(0, At.CreateIntToPtr(Addresses.back(),
                                              Load->getPointerOperandType()));
        At.SetInsertPoint(Load->getParent(), std::next(Load->getIterator()));
        Addresses.push_back(
            At.CreateAdd(Addresses.back(), At.getInt16(1), "stream.next"));
      }
      Phi.setIncomingValueForBlock(Latch, Addresses.back());
      Step->eraseFromParent();
      for (auto *Add : Offsets) {
        int64_t Offset = cast<ConstantInt>(Add->getOperand(1))->getSExtValue();
        int64_t Index = Offset - FirstOffset;
        Value *Address =
            Index >= 0 && Index <= Stride ? Addresses[Index] : nullptr;
        if (!Address || !DT.dominates(cast<Instruction>(Address), Add)) {
          IRBuilder<> At(Add);
          Address = At.CreateAdd(&Phi, At.getInt16(Offset - FirstOffset),
                                 "stream.address");
        }
        Add->replaceAllUsesWith(Address);
        Add->eraseFromParent();
      }
      for (auto *P : LastPointers) {
        IRBuilder<> At(P);
        P->setOperand(0, At.CreateAdd(&Phi, At.getInt16(-FirstOffset),
                                      "stream.original"));
      }
      Changed = true;
    }
  }
  return Changed;
}

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

/// Reduce repeated address, mask and countdown arithmetic in word-sized loops.
/// Forward byte streams advance from their first address. Proven nonempty
/// countdowns test at the end of each iteration. Bounded shift counts admit
/// incremental masks, using bytes when every mask fits. Empty loops and
/// conditional memory accesses retain their execution conditions.
bool Z80PrepareLoops::runOnFunction(Function &F) {
  const auto &ST = *TM.getSubtargetImpl(F);
  if ((!PrepareLoopMasks && !RotateCountedLoops && !RebaseForwardCursors) ||
      skipFunction(F) || !ST.is16Bit() || ST.hasEZ80Ops())
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
                     return !BB.phis().empty() &&
                            isa<ICmpInst>(BB.getFirstNonPHIIt()) &&
                            std::next(BB.getFirstNonPHIIt()) ==
                                BB.getTerminator()->getIterator();
                   });
  bool MayRebase =
      RebaseForwardCursors && llvm::any_of(F, [](BasicBlock &BB) {
        return llvm::any_of(BB.phis(), [](const PHINode &P) {
          return P.getType()->isIntegerTy(16) &&
                 P.getNumIncomingValues() == 2 &&
                 llvm::any_of(P.incoming_values(), [&](const auto &Incoming) {
                   auto *Step = dyn_cast<BinaryOperator>(Incoming.get());
                   auto *C = Step ? dyn_cast<ConstantInt>(Step->getOperand(1))
                                  : nullptr;
                   return Step && Step->getOpcode() == Instruction::Add &&
                          Step->getOperand(0) == &P && C &&
                          C->getSExtValue() >= 4 && C->getSExtValue() <= 32;
                 });
        });
      });
  if ((!PrepareLoopMasks || Shifts.empty()) && !MayRotate && !MayRebase)
    return false;
  DominatorTree DT(F);
  LoopInfo LI(DT);
  bool Changed = MayRebase && rebaseForwardCursors(LI, DT);
  if (!MayRotate && (!PrepareLoopMasks || Shifts.empty()))
    return Changed;
  AssumptionCache AC(F);
  TargetLibraryInfoImpl TLII(TM.getTargetTriple());
  TargetLibraryInfo TLI(TLII, &F);
  ScalarEvolution SE(F, TLI, AC, DT, LI);
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
