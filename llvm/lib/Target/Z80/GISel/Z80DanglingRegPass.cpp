
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
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Constants.h"

#define DEBUG_TYPE "z80-dangling-reg"

using namespace llvm;

namespace {
class Z80DanglingRegPass: public MachineFunctionPass {
public:
  static char ID;

  Z80DanglingRegPass();

  StringRef getPassName() const override {
    return "Z80 danglign reg pass";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace


Z80DanglingRegPass::Z80DanglingRegPass()
    : MachineFunctionPass(ID) {
}

bool Z80DanglingRegPass::runOnMachineFunction(MachineFunction &MF)
{
  bool changes = false;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  for (auto& MBB : MF) {
    std::map<Register, MachineInstr*> DefinedRegs;

    for (auto MII = MBB.begin(), E = MBB.end(); MII != E; ) {
      MachineInstr &MI = *MII++;
      LLVM_DEBUG(dbgs() << "Z80DanglingRegPass: "; MI.dump());

      bool HasRegMask = false;
      for (auto& MO : MI.operands()) {
        if (MO.isRegMask()) {
          LLVM_DEBUG(dbgs() << "Z80DanglingRegPass: encountered regmask, clearing all: "; MI.dump());
          DefinedRegs.clear();
          HasRegMask = true;
          break;
        }
      }

      if (HasRegMask) {
        continue;
      }

      Register Defined;
      if (MI.getNumOperands() > 0) {
        auto& MO0 = MI.getOperand(0);
        if (MO0.isReg() && MO0.isDef() && !MO0.isImplicit()) {
          Register Reg = MO0.getReg();
          if (Reg && !Reg.isVirtual()) {
            Defined = Reg;
          }
        }
      }

      size_t i = 0;
      for (const MachineOperand &MO : MI.operands()) {
        ++i;
        // ignore the defined register when clobbering
        if (i == 1 && Defined)
            continue;
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        if (!Reg || Reg.isVirtual())
          continue;
        for (auto it = DefinedRegs.begin(); it != DefinedRegs.end(); ) {
          if (it->first == Reg || TRI->isSubRegister(it->first, Reg) || TRI->isSubRegister(Reg, it->first)) {
            LLVM_DEBUG(dbgs() << "Z80DanglingRegPass: used register " << TRI->getName(it->first) << ": "; MI.dump());
            it = DefinedRegs.erase(it);
            continue;
          }
          ++it;
        }
      }

      if (Defined) {
        auto fit = DefinedRegs.find(Defined);
        if (fit != DefinedRegs.end()) {
          LLVM_DEBUG(dbgs() << "Z80DanglingRegPass: found unused register: redefined at: " << TRI->getName(Defined) << ": "; MI.dump());
          LLVM_DEBUG(dbgs() << "Z80DanglingRegPass: dropping previous instruction: "; fit->second->dump());
          fit->second->eraseFromParent();
          changes = true;
        } else {
          LLVM_DEBUG(dbgs() << "Z80DanglingRegPass: defined " << TRI->getName(Defined) << ": "; MI.dump());
        }
        DefinedRegs[Defined] = &MI;
      }
    }
  }
  return changes;
}

char Z80DanglingRegPass::ID = 0;

FunctionPass *llvm::createZ80DanglingRegPass() {
  return new Z80DanglingRegPass();
}

static RegisterPass<Z80DanglingRegPass> X("z80-dangling-reg", "Z80 dangling reg optimization",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

