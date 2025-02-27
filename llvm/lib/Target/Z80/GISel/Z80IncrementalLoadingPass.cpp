
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

  struct RegisterEntry {
    const TargetRegisterInfo *TRI;
    Register reg;
    Optional<std::pair<Register, int64_t>> regOff;
    Optional<int64_t> immediate;

    void def(Register Reg, int64_t offset) {
      LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: " << TRI->getName(reg) << " = " << TRI->getName(Reg) << " + " << offset << "\n");
      regOff.emplace(Reg, offset);
      immediate.reset();
    }

    void setImm(int64_t i) {
      LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: " << TRI->getName(reg) << " = " << i << "\n");
      regOff.reset();
      immediate = i;
    }

    void inc() {
      if (immediate) {
        *immediate = *immediate + 1;
        LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: " << TRI->getName(reg) << " = " << *immediate << "\n");
      } else if (regOff) {
        regOff->second = regOff->second + 1;
        LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: " << TRI->getName(reg) << " = " << TRI->getName(regOff->first) << " + " << regOff->second << "\n");
      }
    }
  };

  for (auto& MBB : MF) {
    std::map<Register, RegisterEntry> Regs;

    auto getReg = [&](Register Reg) -> RegisterEntry& {
      assert(Reg && !Reg.isVirtual());
      auto& RE = Regs[Reg];
      RE.TRI = TRI;
      RE.reg = Reg;
      return RE;
    };

    auto clobber = [&](Register Reg) {
      assert(Reg && !Reg.isVirtual());
      for (auto it = Regs.begin(); it != Regs.end(); ) {
        auto& RE = it->second;
        if (RE.reg == Reg || TRI->isSubRegister(RE.reg, Reg) || TRI->isSubRegister(Reg, RE.reg)) {
          LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: clearing " << TRI->getName(RE.reg) << "\n");
          it = Regs.erase(it);
          continue;
        } else if (RE.regOff && (RE.regOff->first == Reg || TRI->isSubRegister(RE.regOff->first, Reg) || TRI->isSubRegister(Reg, RE.regOff->first))) {
          LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: clearing " << TRI->getName(RE.reg) << "\n");
          it = Regs.erase(it);
          continue;
        }
        ++it;
      }
    };

    auto extractImmediate = [](MachineOperand& MO) -> Optional<int64_t> {
      if (MO.isImm()) {
        return MO.getImm();
      } else if (MO.isCImm()) {
        return MO.getCImm()->getZExtValue();
      } else {
        return None;
      }
    };

    for (auto MII = MBB.begin(), E = MBB.end(); MII != E; ) {
      MachineInstr &MI = *MII++;
      LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: "; MI.dump());

      bool HasRegMask = false;
      for (auto& MO : MI.operands()) {
        if (MO.isRegMask()) {
          LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: encountered regmask, clearing all: "; MI.dump());
          Regs.clear();
          HasRegMask = true;
          break;
        }
      }
      if (HasRegMask) {
        continue;
      }

      if (MII != MBB.end() && std::next(MII) != MBB.end()) {
        auto& MI0 = MI;
        auto& MI1 = *MII;
        auto& MI2 = *std::next(MII);

        bool applied = false;

        // look for the pattern
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
          // make sure that $r0 is currently already an offset of $r1
          auto& r0RE = getReg(r0);
          if (!r0RE.regOff || r0RE.regOff->first != r1)
            break;

          int64_t OldOffset = r0RE.regOff->second;
          auto NewOffsetValue = extractImmediate(MI1.getOperand(1));
          if (!NewOffsetValue)
            break;
          int64_t NewOffset = *NewOffsetValue;

          if (NewOffset == OldOffset + 1) {
            --MII;
            MII = MBB.erase(MII);
            MII = MBB.erase(MII);
            MII = MBB.erase(MII);
            MachineInstr *NewMI = BuildMI(MBB, MII, MI1.getDebugLoc(), TII->get(Z80::INC16r), r0)
              .addReg(r0);
            MII = NewMI->getIterator();
            changes = true;
            applied = true;
            LLVM_DEBUG(dbgs() << "Z80IncrementalLoadingPass: replaced 3 instruction with "; NewMI->dump());
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
            break;
          }
        } while(false);
        if (applied)
            continue;
      }

      if (MI.getOpcode() == Z80::COPY) {
        auto& MO0 = MI.getOperand(0);
        auto& MO1 = MI.getOperand(1);
        if (MO0.isReg() && MO1.isReg()) {
          Register Dst = MO0.getReg();
          Register Src = MO1.getReg();
          assert(Dst && !Dst.isVirtual());
          assert(Src && !Src.isVirtual());
          auto fit = Regs.find(Src);
          if (fit != Regs.end()) {
            auto& SrcRE = fit->second;
            if (SrcRE.regOff && SrcRE.regOff->first != Dst) {
              getReg(Dst).def(SrcRE.regOff->first, SrcRE.regOff->second);
            } else if (SrcRE.immediate) {
              getReg(Dst).setImm(*SrcRE.immediate);
            } else {
              getReg(Dst).def(Src, 0);
            }
          } else {
            getReg(Dst).def(Src, 0);
          }
          continue;
        }
      } else if (MI.getOpcode() == Z80::LD16ri) {
        auto& MO0 = MI.getOperand(0);
        auto& MO1 = MI.getOperand(1);
        Register Dst = MO0.getReg();
        assert(Dst && !Dst.isVirtual());
        auto& RE = getReg(Dst);
        if (MO1.isImm()) {
          RE.setImm(MO1.getImm());
        } else if (MO1.isCImm()) {
          RE.setImm(MO1.getCImm()->getZExtValue());
        }
        continue;
      } else if (MI.getOpcode() == Z80::ADD16ao) {
        auto& MO0 = MI.getOperand(0);
        auto& MO1 = MI.getOperand(1);
        auto& MO2 = MI.getOperand(2);
        Register Dst = MO0.getReg();
        Register Dst2 = MO1.getReg();
        Register Src = MO2.getReg();
        assert(Dst && !Dst.isVirtual());
        assert(Dst2 && !Dst2.isVirtual());
        assert(Dst == Dst2);
        assert(Src && !Src.isVirtual());
        auto& DstRE = getReg(Dst);
        auto& SrcRE = getReg(Src);
        clobber(Z80::F);
        if (DstRE.regOff) {
          if (SrcRE.immediate) {
            DstRE.def(DstRE.regOff->first, DstRE.regOff->second + *SrcRE.immediate);
          } else {
            clobber(Dst);
          }
        } else if (DstRE.immediate) {
          clobber(Dst);
        }
        continue;
      } else if (MI.getOpcode() == Z80::INC16r) {
        auto& MO0 = MI.getOperand(0);
        auto& MO1 = MI.getOperand(1);
        Register Dst = MO0.getReg();
        Register Dst2 = MO1.getReg();
        assert(Dst == Dst2);
        auto& RE = getReg(Dst);
        RE.inc();
        continue;
      }

      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.isDef())
          continue;
        // Reg gets defined. Clobber anything that uses it.
        clobber(MO.getReg());
      }
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

