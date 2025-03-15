
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

#define DEBUG_TYPE "z80-blit-folder"

using namespace llvm;

namespace {
class Z80BlitFolder: public MachineFunctionPass {
public:
  static char ID;

  Z80BlitFolder();

  StringRef getPassName() const override {
    return "Z80 blit folder";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace


Z80BlitFolder::Z80BlitFolder()
    : MachineFunctionPass(ID) {
}

bool Z80BlitFolder::runOnMachineFunction(MachineFunction &MF)
{
  bool changes = false;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  std::map<Register, std::vector<MachineInstr*>> todelete;
  std::set<Register> phi_registers;
  for (auto& MBB : MF) {
    MachineInstr *PrevMI = nullptr;
    for (auto& MI : MBB) {
      bool IgnoreUse = false;
      LLVM_DEBUG(dbgs() << "Z80BlitFolder 1st pass: "; MI.dump());
      if (MI.getOpcode() == TargetOpcode::G_PHI) {
        for (auto& Op : MI.operands()) {
          if (Op.isReg()) {
            phi_registers.insert(Op.getReg());
          }
        }
      }
      if (PrevMI) {
        auto& MI0 = *PrevMI;
        auto& MI1 = MI;
        // Find cases of
        //    %292:r8 = COPY $a
        //    $a = COPY %292:r8
        // and mark them for deletion if the register does not get used later
        do {
          if (MI0.getOpcode() != TargetOpcode::COPY) {
            break;
          }
          if (MI0.getFlags() != 0) {
            break;
          }
          if (MI1.getOpcode() != TargetOpcode::COPY) {
            break;
          }
          if (MI0.getOperand(1).getReg() != Z80::A) {
            break;
          }
          if (MI1.getOperand(0).getReg() != Z80::A) {
            break;
          }
          Register Reg = MI0.getOperand(0).getReg();
          if (MI.getOperand(1).getReg() != Reg) {
            break;
          }
          if (!Reg.isVirtual()) {
            break;
          }
          // XXX delete and use MRI.use_empty() instead
          if (phi_registers.count(Reg) != 0) {
            break;
          }
          todelete[Reg] = {&MI0, &MI1};
          IgnoreUse = true;
        } while(false);
        // Fold
        //    %96:g8 = COPY $a
        //    LD8pg %87:a16, %96:g8 :: (store (s8) into %ir.arrayidx14.3.1, !tbaa !10)
        // into
        //    LD8pg %87:a16, $a
        do {
          if (MI0.getOpcode() != TargetOpcode::COPY) {
            break;
          }
          if (MI0.getFlags() != 0) {
            break;
          }
          if (MI1.getOpcode() != Z80::LD8pg) {
            break;
          }
          if (MI0.getOperand(1).getReg() != Z80::A) {
            break;
          }
          Register Reg = MI0.getOperand(0).getReg();
          if (MI1.getOperand(1).getReg() != Reg) {
            break;
          }
          if (!Reg.isVirtual()) {
            break;
          }
          if (phi_registers.count(Reg) != 0) {
            break;
          }
          todelete[Reg] = {&MI0};
          MI1.setDesc(TII->get(Z80::LD8pg));
          MI1.getOperand(1).setReg(Z80::A);
          changes = true;
        } while (false);
        // Fold
        //    %35:g8 = COPY $a 
        //    LD8og %17:i16, 1, %35:g8 :: (store (s8) into %ir.arrayidx14.1, !tbaa !10)
        // into
        //    LD8or %87:a16, 1, $a
        do {
          if (MI0.getOpcode() != TargetOpcode::COPY) {
            break;
          }
          if (MI0.getFlags() != 0) {
            break;
          }
          if (MI1.getOpcode() != Z80::LD8og) {
            break;
          }
          if (MI0.getOperand(1).getReg() != Z80::A) {
            break;
          }
          Register Reg = MI0.getOperand(0).getReg();
          if (MI1.getOperand(2).getReg() != Reg) {
            break;
          }
          if (!Reg.isVirtual()) {
            break;
          }
          if (phi_registers.count(Reg) != 0) {
            break;
          }
          todelete[Reg] = {&MI0};
          MI1.setDesc(TII->get(Z80::LD8or));
          MI1.getOperand(2).setReg(Z80::A);
          changes = true;
        } while (false);
      }

      if (!IgnoreUse) {
        for (auto& Op : MI.operands()) {
          if (!Op.isReg() || !Op.isUse()) {
            continue;
          }
          todelete.erase(Op.getReg());
        }
      }
      PrevMI = &MI;
    }
  }
  for (auto& e : todelete) {
    LLVM_DEBUG(dbgs() << "Z80BlitFolder: dropping register " << Z80Tracker::RegisterEntry::getName(TRI, e.first) << "\n");
    for (auto *MI : e.second) {
      MI->removeFromParent();
      changes = true;
    }
  }

  for (auto& MBB : MF) {
    std::array<MachineInstr*, 3> MIStack = {0};
    for (auto& MI : MBB) {
      if (MIStack[0]) {
        MachineInstr& MI0 = *MIStack[0];
        MachineInstr& MI1 = *MIStack[1];
        MachineInstr& MI2 = *MIStack[2];
        MachineInstr& MI3 = MI;

        // Replace $hl into %57 and iy into %12 in:
        //    $a = LD8ro %12:i16, 4 :: (load (s8) from %ir.uglygep13, !tbaa !10)
        //    AND8ap %57:a16, implicit-def $a, implicit-def $f, implicit $a :: (load (s8) from %ir.add.ptr32, !tbaa !10)
        //    OR8ao %12:i16, -124, implicit-def $a, implicit-def $f, implicit $a :: (load (s8) from %ir.uglygep14, !tbaa !10)
        //    LD8pg %57:a16, $a :: (store (s8) into %ir.add.ptr32, !tbaa !10)
        do {
          if (!(MI0.getOpcode() == Z80::LD8ro || MI0.getOpcode() == Z80::LD8rp) ) {
            break;
          }
          if (!(MI2.getOpcode() == Z80::OR8ao || MI2.getOpcode() == Z80::OR8ap)) {
            break;
          }
          if (MI1.getOpcode() != Z80::AND8ap || MI3.getOpcode() != Z80::LD8pg) {
            break;
          }
          Register SrcReg = MI0.getOperand(1).getReg();
          Register DstReg = MI1.getOperand(0).getReg();
          if (DstReg != MI3.getOperand(0).getReg()) {
            break;
          }
          if (SrcReg != MI2.getOperand(0).getReg()) {
            break;
          }
          MI1.getOperand(0).setReg(Z80::HL);
          MI3.getOperand(0).setReg(Z80::HL);
          //MI3.getOperand(0).setIsKill(true);
          BuildMI(MBB, MI0, MI0.getDebugLoc(), TII->get(TargetOpcode::COPY))
            .addDef(Z80::HL)
            .addReg(DstReg);
          MI0.getOperand(1).setReg(Z80::IY);
          MI2.getOperand(0).setReg(Z80::IY);
          //MI2.getOperand(0).setIsKill(true);
          BuildMI(MBB, MI0, MI0.getDebugLoc(), TII->get(TargetOpcode::COPY))
            .addDef(Z80::IY)
            .addReg(SrcReg);
          LLVM_DEBUG(dbgs() << "Z80BlitFolder: rewrote to use $hl and $iy\n");
          changes = true;
        } while(false);
      }

      for (size_t i = 0; i < MIStack.size() - 1; ++i) {
        MIStack[i] = MIStack[i + 1];
      }
      MIStack.back() = &MI;
    }
  }

  for (auto& MBB : MF) {
    Z80Tracker T(MBB);

    for (auto MII = MBB.begin(), E = MBB.end(); MII != E; ) {
      MachineInstr &MI = *MII++;
      LLVM_DEBUG(dbgs() << "Z80BlitFolder: "; MI.dump());

      // try to fold
      if (MII != MBB.end() && std::next(MII) != MBB.end()) {
        auto& MI0 = MI;

        bool applied = false;

        do {
          if (MI0.getOpcode() != TargetOpcode::COPY) {
            break;
          }
          Register Dst = MI0.getOperand(0).getReg();
          Register Src = MI0.getOperand(1).getReg();
          if (Dst.isVirtual()) {
            break;
          }
          auto& DstRE = T.getReg(Dst);
          auto& SrcRE = T.getReg(Src);
          if (!DstRE.isRegOff()) {
            break;
          }
          if (DstRE.getRegOff().first == Src) {
            LLVM_DEBUG(dbgs() << "Z80BlitFolder: dropping: "; MI.dump());
            MI.removeFromParent();
            applied = true;
            changes = true;
            break;
          }
          if (!SrcRE.isRegOff() || SrcRE.getRegOff().first != DstRE.getRegOff().first) {
            break;
          }
          int64_t Delta = SrcRE.getRegOff().second - DstRE.getRegOff().second;
          if (Delta == 1) {
            MachineInstr *INCMI = BuildMI(MBB, MI0, MI0.getDebugLoc(), TII->get(Z80::INC16r))
              .addDef(Dst)
              .addReg(Dst);
            LLVM_DEBUG(dbgs() << "Z80BlitFolder: added: "; INCMI->dump());
            DstRE.inc(MI0);
            MI0.removeFromParent();
            changes = true;
            applied = true;
            if (MRI.use_empty(Src)) {
              if (MachineInstr *DefMI = MRI.getVRegDef(Src)) {
                LLVM_DEBUG(dbgs() << "Z80BlitFolder: dropping: "; DefMI->dump());
                DefMI->removeFromParent();
              }
            }
            break;
          } else if (Delta == 0) {
            LLVM_DEBUG(dbgs() << "Z80BlitFolder: dropping: "; MI0.dump());
            MI0.removeFromParent();
            changes = true;
            applied = true;
            if (MRI.use_empty(Src)) {
              if (MachineInstr *DefMI = MRI.getVRegDef(Src)) {
                LLVM_DEBUG(dbgs() << "Z80BlitFolder: dropping: "; DefMI->dump());
                DefMI->removeFromParent();
              }
            }
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

char Z80BlitFolder::ID = 0;

FunctionPass *llvm::createZ80BlitFolder() {
  return new Z80BlitFolder();
}

static RegisterPass<Z80BlitFolder> X("z80-blit-folder", "Z80 blit folder",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

