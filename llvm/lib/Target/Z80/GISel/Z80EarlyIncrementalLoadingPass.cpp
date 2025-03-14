
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

#include "Z80Tracker.h"

#define DEBUG_TYPE "z80-early-incremental-loading"

using namespace llvm;

namespace {
class Z80EarlyIncrementalLoadingPass: public MachineFunctionPass {
public:
  static char ID;

  Z80EarlyIncrementalLoadingPass();

  StringRef getPassName() const override {
    return "Z80 early incremental loading pass";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace


Z80EarlyIncrementalLoadingPass::Z80EarlyIncrementalLoadingPass()
    : MachineFunctionPass(ID) {
}

bool Z80EarlyIncrementalLoadingPass::runOnMachineFunction(MachineFunction &MF)
{
  bool changes = false;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (auto& MBB : MF) {
    Z80Tracker T(MBB);

    for (auto MII = MBB.begin(), E = MBB.end(); MII != E; ) {
      MachineInstr &MI = *MII++;
      LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: "; MI.dump());

      T.process(MI);

      // try to fold
      if (MII != MBB.end() && std::next(MII) != MBB.end()) {
        auto& MI0 = MI;

        bool applied = false;

        do {
          // We look for
          //    %11:_(p0) = G_PTR_ADD %3:_, %10:_(s16)
          // And try to replace with known value or offset from a physical register
          if (MI0.getOpcode() != TargetOpcode::G_PTR_ADD)
            break;
          Register Dst = MI0.getOperand(0).getReg();
          Register Src = MI0.getOperand(1).getReg();
          Register Off = MI0.getOperand(2).getReg();
          auto& DstRE = T.getReg(Dst);
          auto& SrcRE = T.getReg(Src);
          auto OffImmValue = getIConstantVRegValWithLookThrough(Off, MRI);
          if (!OffImmValue)
            break;
          int64_t Offset = OffImmValue->Value.getSExtValue();

          Register base;
          if (!SrcRE.isSet()) {
            base = Src;
          } else if (SrcRE.isRegOff()) {
            base = SrcRE.getRegOff().first;
            Offset = SrcRE.getRegOff().second + Offset;
          } else {
            break;
          }
          for (auto& re : T.getRegs()) {
            if (re.first.isVirtual())
              continue;
            auto& RE = re.second;
            if (RE.isRegOff()) {
              if (RE.getRegOff().first == base) {
                int64_t Delta = Offset - RE.getRegOff().second;
                Register copy = RE.getVirtualCopy();
                // register copy and destination values
                T.getReg(copy).def(*RE.getLastUse(), RE.getRegOff().first, RE.getRegOff().second);
                DstRE.def(MI0, RE.getRegOff().first, RE.getRegOff().second + Delta);
                if (Delta == 0) {
                  MI0.setDesc(TII->get(TargetOpcode::COPY));
                  MI0.getOperand(1).setReg(copy);
                  MI0.removeOperand(2);
                  LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: replaced PTR_ADD with COPY: "; MI0.dump());
                } else {
                  MachineIRBuilder MIB(MI0);
                  Register off = MRI.createGenericVirtualRegister(LLT::scalar(16));
#if 0
                  auto LastLDI = RE.getLastDef();
                  BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::G_CONSTANT), off).addImm(Delta);
#else
                  MachineInstr *CONST = BuildMI(MBB, MI0, MI0.getDebugLoc(), TII->get(TargetOpcode::G_CONSTANT), off).addImm(Delta);
#endif
                  LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: inserted G_CONSTANT: "; CONST->dump());
                  MI0.getOperand(1).setReg(copy);
                  MI0.getOperand(2).setReg(off);
                  LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: replaced PTR_ADD parameters: "; MI0.dump());
                }
                changes = true;
                applied = true;
                break;
              }
            }
          }
        } while(false);
        if (applied)
          continue;
      }
    }
  }
  return changes;
}

char Z80EarlyIncrementalLoadingPass::ID = 0;

FunctionPass *llvm::createZ80EarlyIncrementalLoadingPass() {
  return new Z80EarlyIncrementalLoadingPass();
}

static RegisterPass<Z80EarlyIncrementalLoadingPass> X("z80-early-incremental-loading", "Z80 early incremental loading optimization",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

