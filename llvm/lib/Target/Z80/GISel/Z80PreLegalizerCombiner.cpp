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
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "z80-prelegalizer-combiner"

using namespace llvm;
using namespace MIPatternMatch;

#define GET_GICOMBINER_DEPS
#include "Z80GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

namespace {

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

bool Z80PreLegalizerCombinerImpl::tryCombineAll(MachineInstr &MI) const {
  return tryCombineAllImpl(MI);
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

bool Z80PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasFailedISel())
    return false;

  auto &TPC = getAnalysis<TargetPassConfig>();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);

  bool Changed = false;
  // Expand extending loads into a plain load followed by an explicit extend so
  // the legalizer and instruction selector only need to handle G_LOAD.
  for (MachineBasicBlock &MBB : MF) {
    for (auto It = MBB.begin(), End = MBB.end(); It != End;) {
      MachineInstr &MI = *It++;
      unsigned Opc = MI.getOpcode();
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
  return Changed || Impl.combineMachineInstrs();
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
