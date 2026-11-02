//=== lib/CodeGen/GlobalISel/Z80PostSelectCombiner.cpp --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Combine selected instructions before scheduling and register allocation.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "z80-postselect-combiner"

using namespace llvm;

namespace {
class Z80PostSelectCombiner : public MachineFunctionPass {
public:
  static char ID;

  Z80PostSelectCombiner();

  StringRef getPassName() const override { return "Z80 Post Select Combiner"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // end anonymous namespace

void Z80PostSelectCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

Z80PostSelectCombiner::Z80PostSelectCombiner() : MachineFunctionPass(ID) {
  initializeZ80PostSelectCombinerPass(*PassRegistry::getPassRegistry());
}

bool Z80PostSelectCombiner::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &STI = MF.getSubtarget<Z80Subtarget>();
  auto &TII = *STI.getInstrInfo();
  auto &TRI = *STI.getRegisterInfo();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    auto I = MBB.begin(), E = MBB.end();
    while (I != E) {
      MachineInstr &MI = *I;
      ++I;

      /* Reuse flags while result copies still name virtual values. Later
         load folding can replace a result copy with a load directly into A,
         after which the generic peephole pass excludes the physical source.
         The target hook checks every flag consumer before removing a test. */
      Register Src, Src2;
      int64_t Mask, Value;
      if (TII.analyzeCompare(MI, Src, Src2, Mask, Value) && Mask == -1 &&
          Value == 0 &&
          TII.optimizeCompareInstr(MI, Src, Src2, Mask, Value, &MRI)) {
        Changed = true;
        continue;
      }

      switch (unsigned Opc = MI.getOpcode()) {
      case TargetOpcode::INLINEASM: {
        StringRef Text = StringRef(MI.getOperand(0).getSymbolName()).trim();
        unsigned Instruction = Text.equals_insensitive("di") ? Z80::DI
                               : Text.equals_insensitive("ei") ? Z80::EI : 0;
        if (!Instruction ||
            (MI.getOperand(1).getImm() &
             ~(InlineAsm::Extra_HasSideEffects | InlineAsm::Extra_MayLoad |
               InlineAsm::Extra_MayStore)) ||
            llvm::any_of(llvm::drop_begin(MI.operands(), 2),
                         [](const auto &MO) { return !MO.isMetadata(); }))
          break;
        /* Interrupt control has no general-register effects. Expose the real
           instruction so register allocation can retain bank values across
           it, while the original empty assembly keeps its ordering contract. */
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(Instruction));
        MI.getOperand(0).ChangeToES("");
        Changed = true;
        break;
      }
      case TargetOpcode::COPY: {
        Register DstReg = MI.getOperand(0).getReg();
        if (DstReg != Z80::SPS && DstReg != Z80::SPL)
          break;
        Register TmpReg =
            MRI.createVirtualRegister(DstReg == Z80::SPL ? &Z80::A24RegClass
                                                         : &Z80::A16RegClass);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY), TmpReg)
            .add(MI.getOperand(1));
        MI.setDesc(TII.get(DstReg == Z80::SPL ? Z80::LD24sa : Z80::LD16sa));
        MI.removeOperand(0);
        MI.getOperand(0).setReg(TmpReg);
        MachineInstrBuilder(MF, MI).addReg(DstReg, RegState::ImplicitDefine);
        Changed = true;
        break;
      }
      case Z80::PUSH16r:
      case Z80::PUSH24r: {
        if (!STI.hasEZ80Ops())
          break;
        bool IsPush24 = Opc == Z80::PUSH24r;
        Register SrcReg = MI.getOperand(0).getReg();
        if (!MRI.hasOneUse(SrcReg))
          break;
        MachineInstr *SrcMI = MRI.getVRegDef(SrcReg);
        if (!SrcMI ||
            SrcMI->getOpcode() != (IsPush24 ? Z80::LEA24ro : Z80::LEA16ro))
          break;
        MachineOperand &BaseMO = SrcMI->getOperand(1);
        auto NewOff = SrcMI->getOperand(2).getImm();
        if (!BaseMO.isReg() || NewOff) {
          MI.removeOperand(0);
          MI.setDesc(TII.get(IsPush24 ? Z80::PEA24o : Z80::PEA16o));
          MachineInstrBuilder(MF, MI).add(SrcMI->getOperand(1)).addImm(NewOff);
        } else
          MI.getOperand(0).setReg(BaseMO.getReg());
        SrcMI->eraseFromParent();
        Changed = true;
        break;
      }
      default:
        break;
      }
    }
  }

  return Changed;
}

char Z80PostSelectCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(Z80PostSelectCombiner, DEBUG_TYPE,
                      "Combine Z80 machine instrs after inst selection", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig);
INITIALIZE_PASS_DEPENDENCY(InstructionSelect);
INITIALIZE_PASS_END(Z80PostSelectCombiner, DEBUG_TYPE,
                    "Combine Z80 machine instrs after inst selection", false,
                    false)


FunctionPass *llvm::createZ80PostSelectCombiner() {
  return new Z80PostSelectCombiner;
}
