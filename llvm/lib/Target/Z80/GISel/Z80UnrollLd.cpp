
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

#define DEBUG_TYPE "z80-unroll-ld"

using namespace llvm;

namespace {
class Z80UnrollLd: public MachineFunctionPass {
public:
  static char ID;

  Z80UnrollLd();

  StringRef getPassName() const override {
    return "Z80 unroll LD";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace


Z80UnrollLd::Z80UnrollLd()
    : MachineFunctionPass(ID) {
}

bool Z80UnrollLd::runOnMachineFunction(MachineFunction &MF)
{
  bool changes = false;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (auto& MBB : MF) {
    std::array<MachineInstr*, 2> MIStack = {0};
    std::map<Register, std::map<int64_t, Register>> OffsetMap;
    for (auto& MI : MBB) {
      for (size_t i = 0; i < MIStack.size() - 1; ++i) {
        MIStack[i] = MIStack[i + 1];
      }
      MIStack.back() = &MI;

      // Using the OffsetMap turn
      //    LD8og %17:i16, 3, %55:g8 :: (store (s8) into %ir.arrayidx14.3, !tbaa !10)
      // into
      //    LD8pg %147:a16, %55:g8 :: (store (s8) into %ir.arrayidx14.3, !tbaa !10)
      if (MI.getOpcode() == Z80::LD8og) {
        Register Reg = MI.getOperand(0).getReg();
        int64_t OffsetValue = MI.getOperand(1).getImm();
        auto Fit = OffsetMap.find(Reg);
        if (Fit != OffsetMap.end()) {
          auto Fit2 = Fit->second.find(OffsetValue);
          if (Fit2 != Fit->second.end()) {
            LLVM_DEBUG(dbgs() << "Z80UnrollLd: rewriting: "; MI.dump());
            MI.setDesc(TII->get(Z80::LD8pg));
            MI.getOperand(0).setReg(Fit2->second);
            MI.removeOperand(1);
            LLVM_DEBUG(dbgs() << "Z80UnrollLd: rewrote LD8og -> LD8pg: "; MI.dump());
            changes = true;
          }
        }
      }
      if (!MIStack[0]) {
        continue;
      }
      std::array<unsigned, 2> Opcodes;
      for ( size_t i = 0; i < MIStack.size(); ++i )
          Opcodes[i] = MIStack[i]->getOpcode();
      auto& MI0 = *MIStack[0];
      auto& MI1 = *MIStack[1];
      // Look for
      //    %28:g8 = LD8go %17:i16, 1 :: (load (s8) from %ir.arrayidx14.1, !tbaa !10)
      //    %30:g8 = LD8go %12:i16, 1 :: (load (s8) from %ir.uglygep7, !tbaa !10)
      //    ..
      //    LD8og %17:i16, 1
      // and turn first into a LD8gp
      //    %78:g8 = LD8gp %77:a16 :: (load (s8) from %ir.arrayidx14.2.1, !tbaa !10)
      //    %30:g8 = LD8go %12:i16, 1 :: (load (s8) from %ir.uglygep7, !tbaa !10)
      //    ..
      do {
        if (Opcodes != decltype(Opcodes){Z80::LD8go, Z80::LD8go}) {
          break;
        }
        int64_t OffsetValue = MI0.getOperand(2).getImm();
        Register Reg = MI0.getOperand(1).getReg();
        if (MI1.getOperand(1).getReg() == Reg) {
          break;
        }
        bool FoundUsage = false;
        size_t LookupLen = 10;
        for (auto IT = MI.getIterator(); IT != MBB.end() && LookupLen; ++IT, --LookupLen) {
          if (IT->getOpcode() == Z80::LD8og && IT->getOperand(0).getReg() == Reg) {
            if (IT->getOperand(1).getImm() == OffsetValue) {
              FoundUsage = true;
            }
          }
        }
        if (!FoundUsage) {
          break;
        }
        LLVM_DEBUG(dbgs() << "Z80UnrollLd: rewriting: "; MI0.dump());
        Register Offset = MRI.createGenericVirtualRegister(LLT());
        MRI.setRegClass(Offset, &Z80::O16RegClass);

        LLVMContext &Ctx = MF.getFunction().getContext();
        ConstantInt *CImmVal = ConstantInt::get(Type::getInt16Ty(Ctx), OffsetValue);

        MachineInstr *LDMI = BuildMI(MBB, MI0, MI.getDebugLoc(), TII->get(Z80::LD16ri))
          .addDef(Offset)
          .addCImm(CImmVal);

        Register Dst = MRI.createGenericVirtualRegister(LLT());
        MRI.setRegClass(Dst, &Z80::A16RegClass);
        MachineInstr *ADDMI = BuildMI(MBB, MI0, MI.getDebugLoc(), TII->get(Z80::ADD16ao))
          .addDef(Dst)
          .addUse(Reg)
          .addUse(Offset);
        LLVM_DEBUG(dbgs() << "Z80UnrollLd: inserted: "; LDMI->dump());
        LLVM_DEBUG(dbgs() << "Z80UnrollLd: inserted: "; ADDMI->dump());
        MI0.setDesc(TII->get(Z80::LD8gp));
        MI0.getOperand(1).setReg(Dst);
        MI0.removeOperand(2);
        OffsetMap[Reg][OffsetValue] = Dst;
        LLVM_DEBUG(dbgs() << "Z80UnrollLd: rewrote LD8go -> LD8go: "; MI0.dump());
        changes = true;
      } while(false);
    }
  }
  return changes;
}

char Z80UnrollLd::ID = 0;

FunctionPass *llvm::createZ80UnrollLd() {
  return new Z80UnrollLd();
}

static RegisterPass<Z80UnrollLd> X("z80-unroll-ld", "Z80 unroll LD",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

