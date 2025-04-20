
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

#define DEBUG_TYPE "z80-incremental-loading"

using namespace llvm;

namespace {
class Z80IncrementalLoadingPass: public MachineFunctionPass {
public:
  static char ID;

  Z80IncrementalLoadingPass();

  StringRef getPassName() const override {
    return "Z80 incremental loading pass";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace


Z80IncrementalLoadingPass::Z80IncrementalLoadingPass()
    : MachineFunctionPass(ID) {
}

bool Z80IncrementalLoadingPass::runOnMachineFunction(MachineFunction &MF)
{
  bool changes = false;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  for (auto& MBB : MF) {
    Z80Tracker T(MBB);

    for (auto MII = MBB.begin(), E = MBB.end(); MII != E; ) {
      MachineInstr &MI = *MII++;
      LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: "; MI.dump());

      if (MII != MBB.end() && std::next(MII) != MBB.end()) {
        auto& MI0 = MI;
        auto& MI1 = *MII;
        auto& MI2 = *std::next(MII);

        bool applied = false;

        do {
          // We look for
          //    $r0 = LD16ri 253
          // And try to optimize the immediate if we already have on in there
          if (MI0.getOpcode() != Z80::LD16ri)
            break;
          Register r0 = MI0.getOperand(0).getReg();
          auto& r0RE = T.getReg(r0);
          if (!r0RE.isImm())
            break;
          auto Value = T.extractImmediate(MI0.getOperand(1));
          if (!Value)
            break;
          // make sure we know where it was last used
          if (!r0RE.getLastUse())
            break;
          // if exactly same value
          if (*Value == r0RE.getImm()) {
            LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: no change in value of " << TRI->getName(r0) << ", dropping: "; MI0.dump());
            MI0.removeFromParent();
            r0RE.clearLastKill();
            changes = true;
            applied = true;
          }
          // XXX could also optimize increment and decrement
        } while(false);
        if (applied)
          continue;

        do {
          // We look for
          //    $r0 = COPY $r1
          //    $r0 = ADD16ao killed $r0(tied-def 0), killed $r2, implicit-def dead $f
          //    $r1 = COPY killed $r0
          // where $r0 = IX/IY, $r1 = DE and $r2 = BC
          if (!(MI0.getOpcode() == Z80::COPY && MI1.getOpcode() == Z80::ADD16ao && MI2.getOpcode() == Z80::COPY))
            break;
          // sanity
          if (!MI0.getOperand(0).isReg() || !MI0.getOperand(1).isReg())
            break;
          if (!MI1.getOperand(0).isReg() || !MI1.getOperand(1).isReg() || !MI1.getOperand(2).isReg())
            break;
          if (!MI2.getOperand(0).isReg() || !MI2.getOperand(1).isReg())
            break;
          // extract registers in the pattern
          Register r0 = MI0.getOperand(0).getReg();
          Register r1 = MI0.getOperand(1).getReg();
          Register r2 = MI1.getOperand(2).getReg();
          bool isR2Kill = MI1.getOperand(2).isKill();
          // make sure r0 is IX/IY
          if (!(r0 == Z80::IX || r0 == Z80::IY))
            break;
          if (r1 != Z80::DE || r2 != Z80::BC)
            break;
          if (MI2.getOperand(0).getReg() != r1 || MI2.getOperand(1).getReg() != r0)
            break;
          if (MI1.getOperand(0).getReg() != r0 || MI1.getOperand(1).getReg() != r0)
            break;
          // make sure that $r0 is killed at the end
          if (!MI2.getOperand(1).isKill())
            break;
          MachineIRBuilder MIB(MI0);
          Register HL = Z80::HL;
          MachineInstr *EX0 = MIB.buildInstr(Z80::EX16DE);
          MIB.buildInstr(Z80::ADD16aa).addDef(HL).addUse(r2, isR2Kill ? RegState::Kill : 0);
          MIB.buildInstr(Z80::EX16DE);
          MII = EX0->getIterator();
          MI0.eraseFromParent();
          MI1.eraseFromParent();
          MI2.eraseFromParent();
          changes = true;
          applied = true;
          LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: optimized DE = DE + BC\n");
          break;
        } while(false);
        if (applied)
            continue;

        do {
          // We look for
          //    $r0 = COPY $r1
          //    $r2 = LD16ri i16 771
          //    $r0 = ADD16ao killed $r0(tied-def 0), killed $r2, implicit-def dead $f

          if (!(MI0.getOpcode() == Z80::COPY && MI1.getOpcode() == Z80::LD16ri && MI2.getOpcode() == Z80::ADD16ao))
            break;

          // sanity
          if (!MI0.getOperand(0).isReg() || !MI0.getOperand(1).isReg())
            break;
          assert(MI2.getOperand(0).getReg() == MI2.getOperand(1).getReg());

          // extract registers in the pattern
          Register r0 = MI0.getOperand(0).getReg();
          Register r1 = MI0.getOperand(1).getReg();
          Register r2 = MI1.getOperand(0).getReg();

          // sanity
          if (r0 == r1 || r0 == r2 || r1 == r2)
            break;

          // make sure we add into $r0
          if (MI2.getOperand(0).getReg() != r0)
            break;
          // make sure it is $r2 we add
          if (MI2.getOperand(2).getReg() != r2)
            break;
          // make sure that $r2 is killed
          if (!MI2.getOperand(2).isKill())
            break;
          // make sure that $r0 is currently already an offset of $r1
          auto& r0RE = T.getReg(r0);
          if (!r0RE.isRegOff() || r0RE.getRegOff().first != r1)
            break;
          // make sure we know where it was last used
          if (!r0RE.getLastUse())
            break;

          int64_t OldOffset = r0RE.getRegOff().second;
          auto NewOffsetValue = T.extractImmediate(MI1.getOperand(1));
          if (!NewOffsetValue)
            break;
          int64_t NewOffset = *NewOffsetValue;

          int64_t Delta = NewOffset - OldOffset;

          if (Delta == 1 || Delta == 2) {
            --MII;
            MII = MBB.erase(MII);
            MII = MBB.erase(MII);
            MII = MBB.erase(MII);
            MachineInstr *NewMI = BuildMI(MBB, MII, MI1.getDebugLoc(), TII->get(Z80::INC16r), r0)
              .addReg(r0);
            if (Delta == 2) {
              BuildMI(MBB, MII, MI1.getDebugLoc(), TII->get(Z80::INC16r), r0)
                .addReg(r0);
            }
            MII = NewMI->getIterator();
            changes = true;
            applied = true;
            LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: replaced 3 instruction with " << Delta << " X "; NewMI->dump());
            r0RE.clearLastKill();
            break;
          } else {
            --MII;
            MII = MBB.erase(MII);
            MII = MBB.erase(MII);
            MachineInstr *NewMI = BuildMI(MBB, MII, MI1.getDebugLoc(), TII->get(Z80::LD16ri), r2)
              .addImm(NewOffset - OldOffset);
            MII = NewMI->getIterator();
            changes = true;
            applied = true;
            LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: replaced 2 instruction with "; NewMI->dump());
            r0RE.clearLastKill();
            break;
          }
        } while(false);
        if (applied)
            continue;
      }

      T.process(MI);
    }
  }
  return changes;
}

char Z80IncrementalLoadingPass::ID = 0;

FunctionPass *llvm::createZ80IncrementalLoadingPass() {
  return new Z80IncrementalLoadingPass();
}

static RegisterPass<Z80IncrementalLoadingPass> X("z80-incremental-loading", "Z80 incremental loading optimization",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

