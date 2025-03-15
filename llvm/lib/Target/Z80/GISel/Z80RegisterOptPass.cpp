
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "z80-register-opt"

using namespace llvm;

namespace {
class Z80RegisterOptPass : public MachineFunctionPass {
public:
  static char ID;

  Z80RegisterOptPass();

  StringRef getPassName() const override {
    return "Z80 register optimization";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace


Z80RegisterOptPass::Z80RegisterOptPass()
    : MachineFunctionPass(ID) {
}

bool Z80RegisterOptPass::runOnMachineFunction(MachineFunction &MF)
{
  bool changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  for (auto &MBB : MF) {
    for (auto& MI : MBB) {
      auto Opc = MI.getOpcode();
      // turn "$a = LD8rp -> $a = LD8ap"
      do {
        if (Opc != Z80::LD8rp) {
          break;
        }
        if (MI.getOperand(0).getReg() != Z80::A) {
          break;
        }
        Register Src = MI.getOperand(1).getReg();
        if (!Src.isVirtual()) {
          break;
        }
        LLVM_DEBUG(dbgs() << "Z80RegisterOptPass: changing: "; MI.dump());
        MachineIRBuilder MIB(MI);
        Register NewSrc = MRI.createVirtualRegister(&Z80::R16RegClass);
        MachineInstr *Copy = MIB.buildInstr(TargetOpcode::COPY).addDef(NewSrc).addUse(Src);
        LLVM_DEBUG(dbgs() << "@Z80RegisterOptPass: added: "; Copy->dump());
        MI.setDesc(TII->get(Z80::LD8ap));
        MI.getOperand(1).setReg(NewSrc);
        LLVM_DEBUG(dbgs() << "@Z80RegisterOptPass: rewrote to: "; MI.dump());
        changed = true;
      } while(false);
      // XXX maybe others, LD8pr, LD8pg, LD8gp?!?
    }
  }
  return changed;
}

char Z80RegisterOptPass::ID = 0;

FunctionPass *llvm::createZ80RegisterOptPass() {
  return new Z80RegisterOptPass();
}

static RegisterPass<Z80RegisterOptPass> X("z80-register-opt", "Z80 register optimization",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

