//=== lib/CodeGen/GlobalISel/Z80PostLegalizerCombiner.cpp -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// after the legalizer.
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelValueTracking.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "z80-postlegalizer-combiner"

using namespace llvm;

#define GET_GICOMBINER_DEPS
#include "Z80GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

namespace {

#define GET_GICOMBINER_TYPES
#include "Z80GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

class Z80PostLegalizerCombinerImpl : public Combiner {
protected:
  const CombinerHelper Helper;
  const Z80PostLegalizerCombinerImplRuleConfig &RuleConfig;
  const Z80Subtarget &STI;

public:
  Z80PostLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
      const Z80PostLegalizerCombinerImplRuleConfig &RuleConfig,
      const Z80Subtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "Z80PostLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

private:
  bool tryCombineAllImpl(MachineInstr &I) const;
#define GET_GICOMBINER_CLASS_MEMBERS
#include "Z80GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "Z80GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

bool Z80PostLegalizerCombinerImpl::tryCombineAll(MachineInstr &I) const {
  return tryCombineAllImpl(I);
}

Z80PostLegalizerCombinerImpl::Z80PostLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelValueTracking &VT, GISelCSEInfo *CSEInfo,
    const Z80PostLegalizerCombinerImplRuleConfig &RuleConfig,
    const Z80Subtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, TPC, &VT, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ false, &VT, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "Z80GenPostLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

class Z80PostLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  Z80PostLegalizerCombiner(bool IsOptNone = false);

  StringRef getPassName() const override {
    return "Z80 Post-Legalizer Combiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  bool IsOptNone;
  Z80PostLegalizerCombinerImplRuleConfig RuleConfig;
};
} // end anonymous namespace

void Z80PostLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
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

Z80PostLegalizerCombiner::Z80PostLegalizerCombiner(bool IsOptNone)
    : MachineFunctionPass(ID), IsOptNone(IsOptNone) {
  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
  initializeZ80PostLegalizerCombinerPass(*PassRegistry::getPassRegistry());
}

bool Z80PostLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasFailedISel())
    return false;

  auto &TPC = getAnalysis<TargetPassConfig>();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);

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

  Z80PostLegalizerCombinerImpl Impl(MF, CInfo, &TPC, *VT,
                                    /*CSEInfo*/ nullptr, RuleConfig, ST, MDT,
                                    LI);
  return Impl.combineMachineInstrs();
}

char Z80PostLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(Z80PostLegalizerCombiner, DEBUG_TYPE,
                      "Combine Z80 machine instrs after legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelValueTrackingAnalysisLegacy)
INITIALIZE_PASS_END(Z80PostLegalizerCombiner, DEBUG_TYPE,
                    "Combine Z80 machine instrs after legalization", false,
                    false)

FunctionPass *llvm::createZ80PostLegalizeCombiner(bool IsOptNone) {
  return new Z80PostLegalizerCombiner(IsOptNone);
}
