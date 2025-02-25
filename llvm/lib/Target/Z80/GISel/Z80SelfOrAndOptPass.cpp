
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

#define DEBUG_TYPE "z80-self-or-and-opt"

using namespace llvm;

namespace {
class Z80SelfOrAndOptPass : public MachineFunctionPass {
public:
  static char ID;

  Z80SelfOrAndOptPass();

  StringRef getPassName() const override {
    return "Z80 self OR and AND optimization";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace


Z80SelfOrAndOptPass::Z80SelfOrAndOptPass()
    : MachineFunctionPass(ID) {
}

bool Z80SelfOrAndOptPass::runOnMachineFunction(MachineFunction &MF)
{
  // For "OR a, a" "AND a,a" clear the implicit-def flag on the register A as register value is not changed.
  bool changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  for (auto &MBB : MF) {
    for (auto& MI : MBB) {
      auto Opc = MI.getOpcode();
      switch (Opc) {
        case Z80::OR8ar:
        case Z80::AND8ar: {
          if (MI.getOperand(0).getReg() != Z80::A)
              continue;
          for (uint i = 0; i < MI.getNumOperands(); ++i) {
            auto& Op = MI.getOperand(i);
            if (Op.isReg() && Op.getReg() == Z80::A && Op.isDef()) {
              LLVM_DEBUG(dbgs() << "Z80SelfOrAndOptPass: clearing define on A for instruction " << Opc << "\n");
              Op.setIsDef(false);
              changed = true;
            }
          }
          break;
        }
      }
    }
  }
  return changed;
}

char Z80SelfOrAndOptPass::ID = 0;

FunctionPass *llvm::createZ80SelfOrAndOptPass() {
  return new Z80SelfOrAndOptPass();
}

static RegisterPass<Z80SelfOrAndOptPass> X("z80-self-or-and-opt", "Z80 LDI address recovery",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

