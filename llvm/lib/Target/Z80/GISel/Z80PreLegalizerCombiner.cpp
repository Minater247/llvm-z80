//=== lib/CodeGen/GlobalISel/Z80PreLegalizerCombiner.cpp ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelValueTracking.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "z80-prelegalizer-combiner"

using namespace llvm;
using namespace MIPatternMatch;

#define GET_GICOMBINER_DEPS
#include "Z80GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

namespace {

struct ConstantMultiply {
  Register Src;
  unsigned Shift;
  unsigned PostShift;
  bool Subtract;
  bool Reverse;
  bool Negate;
};

struct PageOffset {
  bool Reverse = false;
  bool AddBaseLow = false;
  uint8_t LowOffset = 0;
};

#define GET_GICOMBINER_TYPES
#include "Z80GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

static bool matchCombineTruncShift(MachineInstr &MI, MachineRegisterInfo &MRI,
                                   Register &SrcReg);
static void applyCombineTruncShift(MachineInstr &MI, MachineIRBuilder &Builder,
                                   GISelChangeObserver &Observer,
                                   Register SrcReg);

static bool matchFlipSetCCCond(MachineInstr &MI, MachineRegisterInfo &MRI,
                               MachineInstr *&SetCCMI);
static void applyFlipSetCCCond(MachineInstr &MI, MachineIRBuilder &Builder,
                               GISelChangeObserver &Observer,
                               MachineInstr &SetCCMI);

static bool matchCombineLoadStore(MachineInstr &LoadMI,
                                  MachineRegisterInfo &MRI);
static void applyCombineLoadStore(MachineInstr &LoadMI,
                                  MachineRegisterInfo &MRI,
                                  MachineIRBuilder &Builder,
                                  GISelChangeObserver &Observer);

static bool matchConstantMultiply(MachineInstr &MI, MachineRegisterInfo &MRI,
                                  const Z80Subtarget &ST,
                                  ConstantMultiply &Match);
static void applyConstantMultiply(MachineInstr &MI, MachineIRBuilder &Builder,
                                  GISelChangeObserver &Observer,
                                  const ConstantMultiply &Match);

static bool matchPageOffset(MachineInstr &MI, MachineRegisterInfo &MRI,
                            GISelValueTracking *VT, PageOffset &Match);
static void applyPageOffset(MachineInstr &MI, MachineIRBuilder &Builder,
                            GISelChangeObserver &Observer,
                            const PageOffset &Match);

static bool combineByteRemainder(MachineInstr &MI, MachineIRBuilder &Builder,
                                 GISelChangeObserver &Observer,
                                 const Z80Subtarget &ST);

class Z80PreLegalizerCombinerImpl : public Combiner {
protected:
  const CombinerHelper Helper;
  const Z80PreLegalizerCombinerImplRuleConfig &RuleConfig;
  const Z80Subtarget &STI;

public:
  Z80PreLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
      const Z80PreLegalizerCombinerImplRuleConfig &RuleConfig,
      const Z80Subtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "Z80PreLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

  bool tryCombineAllImpl(MachineInstr &I) const;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "Z80GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "Z80GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

Z80PreLegalizerCombinerImpl::Z80PreLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
    const Z80PreLegalizerCombinerImplRuleConfig &RuleConfig,
    const Z80Subtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, TPC, &VT, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ true, &VT, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "Z80GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

/// Compare extended bytes in their original width. Matching extensions retain
/// their ordering; a comparison of zero-extended bytes uses unsigned ordering.
/// Constants participate only when the same extension reproduces their value.
static bool combineExtendedByteCompare(MachineInstr &MI,
                                       MachineIRBuilder &Builder,
                                       GISelChangeObserver &Observer) {
  if (MI.getOpcode() != TargetOpcode::G_ICMP)
    return false;
  auto &MRI = MI.getMF()->getRegInfo();
  LLT Ty = MRI.getType(MI.getOperand(2).getReg());
  if (!Ty.isScalar() || Ty.getSizeInBits() <= 8)
    return false;
  Register Narrow[2];
  std::optional<APInt> Constants[2];
  unsigned Extension = 0;
  for (unsigned I = 0; I != 2; ++I) {
    Register R = MI.getOperand(I + 2).getReg();
    MachineInstr *Def = getDefIgnoringCopies(R, MRI);
    if (!Def)
      return false;
    unsigned Opc = Def->getOpcode();
    if ((Opc == TargetOpcode::G_ZEXT || Opc == TargetOpcode::G_SEXT) &&
        MRI.getType(Def->getOperand(1).getReg()) == LLT::scalar(8)) {
      if (Extension && Extension != Opc)
        return false;
      Extension = Opc;
      Narrow[I] = Def->getOperand(1).getReg();
    } else {
      Constants[I] = getIConstantVRegVal(R, MRI);
      if (!Constants[I])
        return false;
    }
  }
  if (!Extension)
    return false;
  for (const auto &C : Constants)
    if (C && (Extension == TargetOpcode::G_ZEXT ? !C->isIntN(8)
                                               : !C->isSignedIntN(8)))
      return false;

  Builder.setInstrAndDebugLoc(MI);
  for (unsigned I = 0; I != 2; ++I)
    if (Constants[I])
      Narrow[I] = Builder.buildConstant(LLT::scalar(8), Constants[I]->trunc(8))
                      .getReg(0);
  auto Pred = static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());
  if (Extension == TargetOpcode::G_ZEXT)
    Pred = ICmpInst::getUnsignedPredicate(Pred);
  Builder.buildICmp(Pred, MI.getOperand(0).getReg(), Narrow[0], Narrow[1]);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
  return true;
}

/// Extract masked word bits using byte-width arithmetic. Byte-sized masks
/// selecting one source byte produce a zero-extended result of the original
/// scalar width. Shared shift results and selected sign-fill bits stay intact.
static bool combineMaskedWordShift(MachineInstr &MI, MachineIRBuilder &Builder,
                                   GISelChangeObserver &Observer) {
  if (MI.getOpcode() != TargetOpcode::G_AND)
    return false;
  auto &MRI = MI.getMF()->getRegInfo();
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = MRI.getType(Dst);
  if (!Ty.isScalar() || Ty.getSizeInBits() <= 8 || Ty.getSizeInBits() > 32 ||
      Ty.getSizeInBits() % 8)
    return false;
  Register Shifted = MI.getOperand(1).getReg();
  auto Mask = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI);
  if (!Mask) {
    Shifted = MI.getOperand(2).getReg();
    Mask = getIConstantVRegValWithLookThrough(MI.getOperand(1).getReg(), MRI);
  }
  if (!Mask || Mask->Value.isZero() || !Mask->Value.isIntN(8) ||
      !MRI.hasOneNonDBGUse(Shifted))
    return false;
  auto *Shift = getDefIgnoringCopies(Shifted, MRI);
  if (!Shift || (Shift->getOpcode() != TargetOpcode::G_LSHR &&
                 Shift->getOpcode() != TargetOpcode::G_ASHR))
    return false;
  auto Amount =
      getIConstantVRegValWithLookThrough(Shift->getOperand(2).getReg(), MRI);
  if (!Amount || Amount->Value.uge(Ty.getSizeInBits()))
    return false;
  unsigned Count = Amount->Value.getZExtValue();
  unsigned First = Count + Mask->Value.countr_zero();
  unsigned Last = Count + Mask->Value.getActiveBits() - 1;
  if (Last >= Ty.getSizeInBits() || First / 8 != Last / 8)
    return false;

  Builder.setInstrAndDebugLoc(MI);
  LLT Byte = LLT::scalar(8);
  auto Parts = Builder.buildUnmerge(Byte, Shift->getOperand(1).getReg());
  Register Value = Parts.getReg(First / 8);
  unsigned Base = First & ~7u;
  /* The selected byte can start above the original shift count when only
     high output bits are retained. Shift that byte left into their positions. */
  if (Count > Base)
    Value = Builder
                .buildLShr(Byte, Value, Builder.buildConstant(Byte, Count - Base))
                .getReg(0);
  else if (Count < Base)
    Value = Builder
                .buildShl(Byte, Value, Builder.buildConstant(Byte, Base - Count))
                .getReg(0);
  auto Result = Builder.buildAnd(
      Byte, Value, Builder.buildConstant(Byte, Mask->Value.trunc(8)));
  Builder.buildZExt(Dst, Result);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
  return true;
}

bool Z80PreLegalizerCombinerImpl::tryCombineAll(MachineInstr &MI) const {
  /* Materialize the known shift count directly. The generic power-of-two
     combine introduces a CTTZ that would otherwise need a software helper. */
  if (MI.getOpcode() == TargetOpcode::G_UDIV ||
      MI.getOpcode() == TargetOpcode::G_UREM) {
    auto D = getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI);
    if (D && D->Value.getBitWidth() <= 64 && D->Value.isPowerOf2()) {
      B.setInstrAndDebugLoc(MI);
      LLT Ty = MRI.getType(MI.getOperand(0).getReg());
      if (MI.getOpcode() == TargetOpcode::G_UDIV)
        B.buildLShr(MI.getOperand(0).getReg(), MI.getOperand(1).getReg(),
                    B.buildConstant(LLT::scalar(8), D->Value.exactLogBase2()));
      else
        B.buildAnd(MI.getOperand(0).getReg(), MI.getOperand(1).getReg(),
                   B.buildConstant(Ty, D->Value - 1));
      Observer.erasingInstr(MI);
      MI.eraseFromParent();
      return true;
    }
  }
  if (combineMaskedWordShift(MI, B, Observer))
    return true;
  if (combineExtendedByteCompare(MI, B, Observer))
    return true;
  if (combineByteRemainder(MI, B, Observer, STI))
    return true;
  return tryCombineAllImpl(MI);
}

/// Reuse the remainder from a constant byte division for x - (x / d) * d.
/// The division and reconstruction must share a block and identical operands.
static bool combineByteRemainder(MachineInstr &MI, MachineIRBuilder &Builder,
                                 GISelChangeObserver &Observer,
                                 const Z80Subtarget &ST) {
  if (MI.getOpcode() != TargetOpcode::G_MUL || ST.hasZ180Ops() ||
      ST.hasEZ80Ops())
    return false;
  auto &MRI = *Builder.getMRI();
  Register Product = MI.getOperand(0).getReg();
  if (MRI.getType(Product) != LLT::scalar(8) || !MRI.hasOneNonDBGUse(Product))
    return false;
  MachineInstr &Sub = *MRI.use_nodbg_instructions(Product).begin();
  if (Sub.getOpcode() != TargetOpcode::G_SUB ||
      Sub.getOperand(2).getReg() != Product ||
      Sub.getParent() != MI.getParent())
    return false;
  for (unsigned Op : {1u, 2u}) {
    auto *Div = getDefIgnoringCopies(MI.getOperand(Op).getReg(), MRI);
    if (!Div || Div->getOpcode() != TargetOpcode::G_UDIV ||
        Div->getParent() != MI.getParent() ||
        Div->getOperand(1).getReg() != Sub.getOperand(1).getReg())
      continue;
    auto D =
        getIConstantVRegValWithLookThrough(Div->getOperand(2).getReg(), MRI);
    auto M =
        getIConstantVRegValWithLookThrough(MI.getOperand(3 - Op).getReg(), MRI);
    if (!D || !M || D->Value != M->Value || D->Value.getZExtValue() < 8)
      continue;
    /* Recover the two-result operation before constant multiplication becomes
       shifts and adds. Its loop already computes both results. */
    Builder.setInstrAndDebugLoc(*Div);
    Builder.buildInstr(
        TargetOpcode::G_UDIVREM,
        {Div->getOperand(0).getReg(), Sub.getOperand(0).getReg()},
        {Div->getOperand(1).getReg(), Div->getOperand(2).getReg()});
    Observer.erasingInstr(*Div);
    Div->eraseFromParent();
    Observer.erasingInstr(Sub);
    Sub.eraseFromParent();
    Observer.erasingInstr(MI);
    MI.eraseFromParent();
    return true;
  }
  return false;
}

/// Recognize byte or native-pair products that use at most four doublings and
/// one add or subtract, plus an optional byte negation, preserving modular bits.
/// Factor out powers of two to include even constants such as 6, 12 and 24.
/// Hardware byte multiplication remains available on Z180 and eZ80 targets.
static bool matchConstantMultiply(MachineInstr &MI, MachineRegisterInfo &MRI,
                                  const Z80Subtarget &ST,
                                  ConstantMultiply &Match) {
  LLT Ty = MRI.getType(MI.getOperand(0).getReg());
  if (!(Ty == LLT::scalar(8) && !ST.hasZ180Ops()) && Ty != LLT::scalar(16) &&
      !(ST.is24Bit() && Ty == LLT::scalar(24)))
    return false;
  auto Factor =
      getIConstantVRegValWithLookThrough(MI.getOperand(2).getReg(), MRI);
  if (!Factor)
    return false;
  bool Negative = Factor->Value.isNegative();
  uint64_t Value = (Negative ? -Factor->Value : Factor->Value).getLimitedValue();
  if (Value < 3 || Value > 32)
    return false;
  unsigned PostShift = llvm::countr_zero(Value);
  Value >>= PostShift;
  if (Value == 1)
    return false;
  bool Subtract = !isPowerOf2_64(Value - 1);
  /* Pair negation can require a runtime call when optimizing for size.
     Express negative pair factors directly as x - (x << k) instead. */
  if (Negative && Ty != LLT::scalar(8))
    Subtract = true;
  uint64_t Power = Subtract ? Value + 1 : Value - 1;
  if (!isPowerOf2_64(Power))
    return false;
  unsigned Shift = Log2_64(Power);
  if (Shift + PostShift > 4)
    return false;
  Match = {MI.getOperand(1).getReg(), Shift, PostShift, Subtract,
           Negative && Subtract, Negative && !Subtract};
  return true;
}

static void applyConstantMultiply(MachineInstr &MI, MachineIRBuilder &Builder,
                                  GISelChangeObserver &Observer,
                                  const ConstantMultiply &Match) {
  Builder.setInstrAndDebugLoc(MI);
  Register Dst = MI.getOperand(0).getReg();
  LLT Ty = Builder.getMRI()->getType(Dst);
  auto Amount = Builder.buildConstant(LLT::scalar(8), Match.Shift);
  auto Shifted = Builder.buildShl(Ty, Match.Src, Amount);
  Register Partial = Match.PostShift || Match.Negate
                         ? Builder.getMRI()->createGenericVirtualRegister(Ty)
                         : Dst;
  /* The intermediate shift may wrap even when the original product has a
     no-wrap flag. Keep the new operations modular instead of copying flags. */
  if (Match.Subtract) {
    if (Match.Reverse)
      Builder.buildSub(Partial, Match.Src, Shifted);
    else
      Builder.buildSub(Partial, Shifted, Match.Src);
  } else
    Builder.buildAdd(Partial, Shifted, Match.Src);
  Register Product = Partial;
  if (Match.PostShift) {
    auto PostAmount = Builder.buildConstant(LLT::scalar(8), Match.PostShift);
    Product = Match.Negate
                  ? Builder.getMRI()->createGenericVirtualRegister(Ty)
                  : Dst;
    Builder.buildShl(Product, Partial, PostAmount);
  }
  if (Match.Negate) {
    auto Zero = Builder.buildConstant(Ty, 0);
    Builder.buildSub(Dst, Zero, Product);
  }
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

/// Recognize disjoint high and low bytes in a 16-bit pointer sum in either
/// order. Bases with a known high byte use byte arithmetic when the offset
/// cannot carry into it. Constant offsets retain existing immediate forms.
static bool matchPageOffset(MachineInstr &MI, MachineRegisterInfo &MRI,
                            GISelValueTracking *VT, PageOffset &Match) {
  LLT Ty = MRI.getType(MI.getOperand(0).getReg());
  if (!VT || !Ty.isPointer() || Ty.getSizeInBits() != 16)
    return false;
  Match = {};
  Register Base = MI.getOperand(1).getReg();
  Register Offset = MI.getOperand(2).getReg();
  auto OffsetBits = VT->getKnownBits(Offset);
  bool ByteOffset = OffsetBits.countMinLeadingZeros() >= 8;
  bool AlignedOffset = OffsetBits.countMinTrailingZeros() >= 8;
  if (!ByteOffset && !AlignedOffset)
    return false;
  auto BaseBits = VT->getKnownBits(Base);
  if (ByteOffset && !getIConstantVRegValWithLookThrough(Offset, MRI) &&
      BaseBits.countMinTrailingZeros() >= 8) {
    Match.Reverse = false;
    return true;
  }
  if (AlignedOffset && !getIConstantVRegValWithLookThrough(Base, MRI) &&
      BaseBits.countMinLeadingZeros() >= 8) {
    Match.Reverse = true;
    return true;
  }
  if (BaseBits.isConstant() && ByteOffset &&
      !getIConstantVRegValWithLookThrough(Offset, MRI)) {
    unsigned Low = BaseBits.getConstant().getZExtValue() & 255;
    if (OffsetBits.getMaxValue().getZExtValue() + Low <= 255) {
      Match.LowOffset = Low;
      return true;
    }
  }
  if (ByteOffset && !getIConstantVRegValWithLookThrough(Offset, MRI) &&
      (BaseBits.Zero | BaseBits.One).countLeadingOnes() >= 8 &&
      (BaseBits.getMaxValue().getZExtValue() & 255) +
              OffsetBits.getMaxValue().getZExtValue() <=
          255) {
    Match.AddBaseLow = true;
    return true;
  }
  return false;
}

static void applyPageOffset(MachineInstr &MI, MachineIRBuilder &Builder,
                            GISelChangeObserver &Observer,
                            const PageOffset &Match) {
  Builder.setInstrAndDebugLoc(MI);
  LLT Byte = LLT::scalar(8), Word = LLT::scalar(16);
  auto Base = Builder.buildPtrToInt(Word, MI.getOperand(1).getReg());
  Register HighWord =
      Match.Reverse ? MI.getOperand(2).getReg() : Base.getReg(0);
  Register LowWord = Match.Reverse ? Base.getReg(0) : MI.getOperand(2).getReg();
  auto Shift = Builder.buildConstant(Byte, 8);
  auto High = Builder.buildTrunc(Byte, Builder.buildLShr(Word, HighWord, Shift));
  Register Low = Builder.buildTrunc(Byte, LowWord).getReg(0);
  if (Match.LowOffset) {
    auto Offset = Builder.buildConstant(Byte, Match.LowOffset);
    Low = Builder.buildAdd(Byte, Low, Offset).getReg(0);
  }
  if (Match.AddBaseLow)
    Low = Builder.buildAdd(Byte, Low, Builder.buildTrunc(Byte, Base)).getReg(0);
  /* Proven absence of a carry lets the high byte stay unchanged. Only the
     low byte needs arithmetic when the base has a nonzero page offset. */
  auto Address = Builder.buildMergeValues(Word, {Low, High.getReg(0)});
  Builder.buildIntToPtr(MI.getOperand(0).getReg(), Address);
  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

static bool matchCombineTruncShift(MachineInstr &MI, MachineRegisterInfo &MRI,
                                   Register &SrcReg) {
  Register DstReg = MI.getOperand(0).getReg();
  LLT DstTy = MRI.getType(DstReg);
  if (DstTy != LLT::scalar(8))
    return false;
  int64_t ShiftAmt;
  if (!mi_match(DstReg, MRI,
                m_GTrunc(m_GLShr(m_Reg(SrcReg), m_ICst(ShiftAmt)))) ||
      ShiftAmt != 8)
    return false;
  LLT SrcTy = MRI.getType(SrcReg);
  return SrcTy.isScalar() && SrcTy.getSizeInBits() >= 16;
}

static void applyCombineTruncShift(MachineInstr &MI, MachineIRBuilder &Builder,
                                   GISelChangeObserver &Observer,
                                   Register SrcReg) {
  Builder.setInstrAndDebugLoc(MI);
  Builder.buildExtract(MI.getOperand(0), SrcReg, 8);

  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

static bool matchFlipSetCCCond(MachineInstr &MI, MachineRegisterInfo &MRI,
                               MachineInstr *&SetCCMI) {
  int64_t Imm;
  return mi_match(MI.getOperand(0).getReg(), MRI,
                  m_GXor(m_OneUse(m_MInstr(SetCCMI)), m_ICst(Imm))) &&
         SetCCMI->getOpcode() == Z80::SetCC && Imm;
}

static void applyFlipSetCCCond(MachineInstr &MI, MachineIRBuilder &Builder,
                               GISelChangeObserver &Observer,
                               MachineInstr &SetCCMI) {
  // Warning, SetCC has a physreg use, so don't create the SetCC at the G_XOR!
  Observer.changingInstr(SetCCMI);
  SetCCMI.getOperand(0).setReg(MI.getOperand(0).getReg());
  MachineOperand &Cond = SetCCMI.getOperand(1);
  Cond.setImm(Z80::GetOppositeBranchCondition(Z80::CondCode(Cond.getImm())));
  Observer.changedInstr(SetCCMI);

  Observer.erasingInstr(MI);
  MI.eraseFromParent();
}

static bool matchCombineLoadStore(MachineInstr &LoadMI,
                                  MachineRegisterInfo &MRI) {
  MachineBasicBlock &MBB = *LoadMI.getParent();
  auto It = LoadMI.getIterator();
  if (It == MBB.end())
    return false;
  MachineInstr &StoreMI = *std::next(It);
  if (StoreMI.getOpcode() != TargetOpcode::G_STORE)
    return false;
  Register Reg = LoadMI.getOperand(0).getReg();
  if (StoreMI.getOperand(0).getReg() != Reg)
    return false;
  LLT RegTy = MRI.getType(Reg);
  return RegTy.getSizeInBytes() >= 2;
}

static void applyCombineLoadStore(MachineInstr &LoadMI,
                                  MachineRegisterInfo &MRI,
                                  MachineIRBuilder &Builder,
                                  GISelChangeObserver &Observer) {
  MachineInstr &StoreMI = *std::next(LoadMI.getIterator());
  Register Reg = LoadMI.getOperand(0).getReg();
  MachineMemOperand *DstMMO = *StoreMI.memoperands_begin();
  MachineMemOperand *SrcMMO = *LoadMI.memoperands_begin();
  LLT RegTy = MRI.getType(Reg);
  uint64_t Size = RegTy.getSizeInBytes();
  Builder.setInstrAndDebugLoc(LoadMI);
  auto SizeReg = Builder.buildConstant(LLT::scalar(16), Size);
  Builder.buildMemCpy(StoreMI.getOperand(1), LoadMI.getOperand(1),
                      SizeReg.getReg(0), *DstMMO, *SrcMMO);
  Observer.erasingInstr(StoreMI);
  StoreMI.eraseFromParent();
  Observer.erasingInstr(LoadMI);
  LoadMI.eraseFromParent();
}

class Z80PreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  Z80PreLegalizerCombiner(bool IsOptNone = false);

  StringRef getPassName() const override {
    return "Z80 Pre-Legalizer Combiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  bool IsOptNone;
  Z80PreLegalizerCombinerImplRuleConfig RuleConfig;
};
} // end anonymous namespace

void Z80PreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelValueTrackingAnalysisLegacy>();
  AU.addPreserved<GISelValueTrackingAnalysisLegacy>();
  if (!IsOptNone) {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
  }
  MachineFunctionPass::getAnalysisUsage(AU);
}

Z80PreLegalizerCombiner::Z80PreLegalizerCombiner(bool IsOptNone)
    : MachineFunctionPass(ID), IsOptNone(IsOptNone) {
  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
  initializeZ80PreLegalizerCombinerPass(*PassRegistry::getPassRegistry());
}

/// Compute word arithmetic and loop values in bytes when all observations
/// discard the upper byte. Full-width consumers keep their original values.
static bool narrowByteOnlyValues(MachineFunction &MF) {
  auto &MRI = MF.getRegInfo();
  SmallVector<MachineInstr *, 32> Candidates, Worklist;
  SmallPtrSet<MachineInstr *, 32> Narrow;
  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      switch (MI.getOpcode()) {
      case TargetOpcode::G_ADD:
      case TargetOpcode::G_SUB:
      case TargetOpcode::G_AND:
      case TargetOpcode::G_OR:
      case TargetOpcode::G_XOR:
      case TargetOpcode::G_PHI:
        if (MRI.getType(MI.getOperand(0).getReg()) == LLT::scalar(16)) {
          Candidates.push_back(&MI);
          Narrow.insert(&MI);
        }
        break;
      default:
        break;
      }
    }
  auto ObservesByte = [&](const MachineInstr &Use, Register R) {
    if (Use.getOpcode() == TargetOpcode::G_TRUNC)
      return MRI.getType(Use.getOperand(0).getReg()) == LLT::scalar(8);
    if (Use.getOpcode() != TargetOpcode::G_AND)
      return false;
    Register Other = Use.getOperand(1).getReg() == R
                         ? Use.getOperand(2).getReg()
                         : Use.getOperand(1).getReg();
    auto Mask = getIConstantVRegVal(Other, MRI);
    return Mask && Mask->isIntN(8);
  };
  for (auto *MI : Candidates) {
    Register R = MI->getOperand(0).getReg();
    if (llvm::any_of(MRI.use_nodbg_instructions(R), [&](const auto &Use) {
          return !Narrow.contains(&Use) && !ObservesByte(Use, R);
        }))
      Worklist.push_back(MI);
  }
  /* Propagate full-width demand backwards through the candidate graph.
     Cyclic PHIs remain narrow only when every path to an observable use
     discards the high byte. Each rejected instruction is visited once. */
  while (!Worklist.empty()) {
    auto *MI = Worklist.pop_back_val();
    if (!Narrow.erase(MI))
      continue;
    for (const auto &MO : MI->uses()) {
      if (!MO.isReg() || !MO.getReg().isVirtual() ||
          ObservesByte(*MI, MO.getReg()))
        continue;
      auto *Def = MRI.getVRegDef(MO.getReg());
      if (Narrow.contains(Def))
        Worklist.push_back(Def);
    }
  }
  if (Narrow.empty())
    return false;
  DenseMap<Register, Register> Bytes;
  LLT Byte = LLT::scalar(8);
  for (auto *MI : Candidates)
    if (Narrow.contains(MI))
      Bytes[MI->getOperand(0).getReg()] =
          MRI.createGenericVirtualRegister(Byte);
  MachineIRBuilder B(MF);
  auto Low = [&](Register R) {
    auto It = Bytes.find(R);
    return It == Bytes.end() ? B.buildTrunc(Byte, R).getReg(0) : It->second;
  };
  for (auto *MI : Candidates) {
    if (!Narrow.contains(MI))
      continue;
    Register Dst = MI->getOperand(0).getReg(), Result = Bytes.lookup(Dst);
    B.setInstrAndDebugLoc(*MI);
    if (MI->getOpcode() == TargetOpcode::G_PHI) {
      SmallVector<std::pair<Register, MachineBasicBlock *>, 4> Incoming;
      for (unsigned I = 1; I < MI->getNumOperands(); I += 2) {
        auto *Pred = MI->getOperand(I + 1).getMBB();
        B.setInsertPt(*Pred, Pred->getFirstTerminator());
        Incoming.emplace_back(Low(MI->getOperand(I).getReg()), Pred);
      }
      B.setInstrAndDebugLoc(*MI);
      auto Phi = B.buildInstr(TargetOpcode::G_PHI, {Result}, {});
      for (auto [R, Pred] : Incoming)
        Phi.addUse(R).addMBB(Pred);
      B.setInsertPt(*MI->getParent(), MI->getParent()->getFirstNonPHI());
    } else {
      Register Left = Low(MI->getOperand(1).getReg());
      Register Right = Low(MI->getOperand(2).getReg());
      B.buildInstr(MI->getOpcode(), {Result}, {Left, Right});
    }
    /* Boundary users observe only the low byte. Reconstruct its word type
       for those users; internal graph edges use the byte directly. Overflow
       flags on the original word arithmetic do not apply to the byte. */
    B.buildZExt(Dst, Result);
    MRI.markUsesInDebugValueAsUndef(Dst);
    MI->eraseFromParent();
  }
  return true;
}

/// Narrow counters whose overflow ends a single-block loop within 256 steps.
/// The counter is private to its recurrence, so its byte value and zero test
/// preserve the iteration count, including the 256-iteration boundary case.
static bool narrowWrappingCounters(MachineFunction &MF) {
  auto &MRI = MF.getRegInfo();
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.succ_size() != 2 || !MBB.isSuccessor(&MBB))
      continue;
    auto Branch = MBB.getFirstTerminator();
    if (Branch == MBB.end() || Branch->getOpcode() != TargetOpcode::G_BRCOND ||
        Branch->getOperand(1).getMBB() == &MBB)
      continue;
    auto Backedge = std::next(Branch);
    if (Backedge == MBB.end() || Backedge->getOpcode() != TargetOpcode::G_BR ||
        Backedge->getOperand(0).getMBB() != &MBB)
      continue;
    Register Overflow = Branch->getOperand(0).getReg();
    MachineInstr *Add = MRI.getVRegDef(Overflow);
    if (!Add || Add->getOpcode() != TargetOpcode::G_UADDO ||
        Add->getParent() != &MBB || !MRI.hasOneNonDBGUse(Overflow))
      continue;
    Register Sum = Add->getOperand(0).getReg();
    Register Counter = Add->getOperand(2).getReg();
    LLT Ty = MRI.getType(Counter);
    if (!Ty.isScalar() || Ty.getSizeInBits() <= 8 || Ty.getSizeInBits() > 24)
      continue;
    auto Step =
        getIConstantVRegValWithLookThrough(Add->getOperand(3).getReg(), MRI);
    MachineInstr *Phi = MRI.getVRegDef(Counter);
    if (!Step || !Step->Value.isOne() || !Phi ||
        Phi->getOpcode() != TargetOpcode::G_PHI || Phi->getParent() != &MBB ||
        Phi->getNumOperands() != 5)
      continue;
    unsigned LoopOp = Phi->getOperand(2).getMBB() == &MBB ? 1 : 3;
    unsigned EntryOp = LoopOp == 1 ? 3 : 1;
    if (Phi->getOperand(LoopOp + 1).getMBB() != &MBB ||
        Phi->getOperand(EntryOp + 1).getMBB() == &MBB)
      continue;
    Register Recurrence = Phi->getOperand(LoopOp).getReg();
    MachineInstr *Increment = MRI.getVRegDef(Recurrence);
    if (Recurrence != Sum) {
      if (!MRI.use_nodbg_empty(Sum) || !Increment ||
          Increment->getOpcode() != TargetOpcode::G_ADD ||
          Increment->getParent() != &MBB ||
          Increment->getOperand(1).getReg() != Counter)
        continue;
      auto IncrementStep = getIConstantVRegValWithLookThrough(
          Increment->getOperand(2).getReg(), MRI);
      if (!IncrementStep || !IncrementStep->Value.isOne())
        continue;
    }
    if (!MRI.hasOneNonDBGUse(Recurrence) ||
        llvm::any_of(MRI.use_nodbg_instructions(Counter),
                     [&](const MachineInstr &Use) {
                       return &Use != Add && &Use != Increment;
                     }))
      continue;
    auto Initial = getIConstantVRegValWithLookThrough(
        Phi->getOperand(EntryOp).getReg(), MRI);
    if (!Initial || !Initial->Value.isNegative() ||
        Initial->Value.getSExtValue() < -256)
      continue;

    /* The high bits remain all ones until the terminating addition. A zero
       byte result therefore identifies exactly the original word overflow,
       and exposes INC's zero flag without keeping a word-sized addend live. */
    MachineBasicBlock *Entry = Phi->getOperand(EntryOp + 1).getMBB();
    MachineIRBuilder Builder(MF);
    Builder.setInsertPt(*Entry, Entry->getFirstTerminator());
    LLT Byte = LLT::scalar(8);
    auto Start = Builder.buildConstant(Byte, Initial->Value.trunc(8));
    auto One = Builder.buildConstant(Byte, 1);
    auto Zero = Builder.buildConstant(Byte, 0);
    Register NarrowCounter = MRI.createGenericVirtualRegister(Byte);
    Register NarrowSum = MRI.createGenericVirtualRegister(Byte);
    Builder.setInstrAndDebugLoc(*Phi);
    Builder.buildInstr(TargetOpcode::G_PHI, {NarrowCounter}, {})
        .addUse(NarrowSum)
        .addMBB(&MBB)
        .addUse(Start.getReg(0))
        .addMBB(Entry);
    Builder.setInstrAndDebugLoc(*Add);
    Builder.buildAdd(NarrowSum, NarrowCounter, One);
    Builder.buildICmp(CmpInst::ICMP_EQ, Overflow, NarrowSum, Zero);
    MRI.markUsesInDebugValueAsUndef(Counter);
    MRI.markUsesInDebugValueAsUndef(Sum);
    MRI.markUsesInDebugValueAsUndef(Recurrence);
    Phi->eraseFromParent();
    if (Increment != Add)
      Increment->eraseFromParent();
    Add->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

/// Express a byte increment's overflow branch as a test for a zero result.
/// Preserve the sum and the wraparound outcome for every input byte.
static bool combineByteIncrementBranch(MachineInstr &MI,
                                       MachineRegisterInfo &MRI) {
  if (MI.getOpcode() != TargetOpcode::G_UADDO ||
      MRI.getType(MI.getOperand(0).getReg()) != LLT::scalar(8))
    return false;
  Register Overflow = MI.getOperand(1).getReg();
  if (!MRI.hasOneNonDBGUse(Overflow) ||
      MRI.use_nodbg_begin(Overflow)->getParent()->getOpcode() !=
          TargetOpcode::G_BRCOND)
    return false;
  Register Value = MI.getOperand(2).getReg();
  Register Step = MI.getOperand(3).getReg();
  auto Constant = getIConstantVRegValWithLookThrough(Step, MRI);
  if (!Constant || !Constant->Value.isOne()) {
    std::swap(Value, Step);
    Constant = getIConstantVRegValWithLookThrough(Step, MRI);
    if (!Constant || !Constant->Value.isOne())
      return false;
  }

  /* INC provides this zero flag in any byte register. An unsigned add's
     carry flag would instead constrain the increment to the accumulator. */
  MachineIRBuilder Builder(MI);
  Register Sum = MI.getOperand(0).getReg();
  Builder.buildAdd(Sum, Value, Step);
  auto Zero = Builder.buildConstant(LLT::scalar(8), 0);
  Builder.buildICmp(CmpInst::ICMP_EQ, Overflow, Sum, Zero);
  MI.eraseFromParent();
  return true;
}

bool Z80PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasFailedISel())
    return false;

  auto &TPC = getAnalysis<TargetPassConfig>();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);

  bool Changed = EnableOpt && narrowWrappingCounters(MF);
  // Expand extending loads into a plain load followed by an explicit extend so
  // the legalizer and instruction selector only need to handle G_LOAD.
  for (MachineBasicBlock &MBB : MF) {
    for (auto It = MBB.begin(), End = MBB.end(); It != End;) {
      MachineInstr &MI = *It++;
      unsigned Opc = MI.getOpcode();
      if (EnableOpt && combineByteIncrementBranch(MI, MRI)) {
        Changed = true;
        continue;
      }
      if (Opc != TargetOpcode::G_ZEXTLOAD && Opc != TargetOpcode::G_SEXTLOAD)
        continue;

      auto *MMO = *MI.memoperands_begin();
      LLT MemTy = MMO->getMemoryType();
      Register PtrReg = MI.getOperand(1).getReg();
      Register DstReg = MI.getOperand(0).getReg();
      LLT DstTy = MRI.getType(DstReg);

      MachineIRBuilder Builder(MI);
      auto Load = Builder.buildLoad(MemTy, PtrReg, *MMO);
      Register LoadReg = Load.getReg(0);

      if (DstTy == MemTy)
        Builder.buildCopy(DstReg, LoadReg);
      else if (DstTy.getSizeInBits() > MemTy.getSizeInBits())
        Builder.buildInstr(Opc == TargetOpcode::G_ZEXTLOAD
                               ? TargetOpcode::G_ZEXT
                               : TargetOpcode::G_SEXT,
                           {DstReg}, {LoadReg});
      else
        Builder.buildTrunc(DstReg, LoadReg);

      MI.eraseFromParent();
      Changed = true;
    }
  }

  GISelValueTracking *VT =
      &getAnalysis<GISelValueTrackingAnalysisLegacy>().get(MF);
  MachineDominatorTree *MDT = nullptr;
  if (!IsOptNone)
    MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();

  const Z80Subtarget &ST = MF.getSubtarget<Z80Subtarget>();
  const auto *LI = ST.getLegalizerInfo();

  CombinerInfo CInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, EnableOpt, F.hasOptSize(),
                     F.hasMinSize());
  CInfo.MaxIterations = 1;
  CInfo.ObserverLvl = CombinerInfo::ObserverLevel::SinglePass;
  CInfo.EnableFullDCE = true;

  Z80PreLegalizerCombinerImpl Impl(MF, CInfo, &TPC, *VT,
                                   /*CSEInfo*/ nullptr, RuleConfig, ST, MDT,
                                   LI);
  Changed |= Impl.combineMachineInstrs();
  /* Local folding first exposes existing byte address arithmetic. Narrow the
     remaining value graph afterwards, preserving those canonical forms and
     their placement relative to calls. */
  if (EnableOpt)
    Changed |= narrowByteOnlyValues(MF);
  return Changed;
}

char Z80PreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(Z80PreLegalizerCombiner, DEBUG_TYPE,
                      "Combine Z80 machine instrs before legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelValueTrackingAnalysisLegacy)
INITIALIZE_PASS_END(Z80PreLegalizerCombiner, DEBUG_TYPE,
                    "Combine Z80 machine instrs before legalization", false,
                    false)

FunctionPass *llvm::createZ80PreLegalizeCombiner(bool IsOptNone) {
  return new Z80PreLegalizerCombiner(IsOptNone);
}
