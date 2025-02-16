
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

#define DEBUG_TYPE "z80-ldi-opt"

using namespace llvm;

namespace {
class Z80LDIOptPass : public MachineFunctionPass {
public:
  static char ID;

  Z80LDIOptPass();

  StringRef getPassName() const override {
    return "Z80 LDI address recovery";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

bool isMemcpy(llvm::MachineOperand op) {
  if (!op.isSymbol())
      return false;
  StringRef FuncName = op.getSymbolName();
  if (!FuncName.startswith("_memcpy"))
      return false;

  // Check if the remaining part is a valid number
  StringRef Suffix = FuncName.drop_front(7); // Drop "_memcpy"
  if (Suffix.empty())
      return true;

  return Suffix.size() == 2 && Suffix.find_first_not_of("0123456789") == StringRef::npos;
}
} // end anonymous namespace


Z80LDIOptPass::Z80LDIOptPass()
    : MachineFunctionPass(ID) {
}

bool Z80LDIOptPass::runOnMachineFunction(MachineFunction &MF)
{
  // Find sequences of LDI-s in this pattern:
  //      $de = COPY %1:_(p0)
  //      $hl = COPY %0:_(p0)
  //      LDI16 implicit-def $de, implicit-def $hl, implicit-def $bc, implicit-def $f, implicit $de, implicit $hl
  //      LDI16 implicit-def $de, implicit-def $hl, implicit-def $bc, implicit-def $f, implicit $de, implicit $hl
  //      LDI16 implicit-def $de, implicit-def $hl, implicit-def $bc, implicit-def $f, implicit $de, implicit $hl
  //      %3:_(s16) = G_CONSTANT i16 3
  //      %4:_(p0) = G_PTR_ADD %0:_, %3:_(s16)
  // And make use of known values of HL/DE into:
  //      $de = COPY %1:_(p0)
  //      $hl = COPY %0:_(p0)
  //      LDI16 implicit-def $de, implicit-def $hl, implicit-def $bc, implicit-def $f, implicit $de, implicit $hl
  //      LDI16 implicit-def $de, implicit-def $hl, implicit-def $bc, implicit-def $f, implicit $de, implicit $hl
  //      LDI16 implicit-def $de, implicit-def $hl, implicit-def $bc, implicit-def $f, implicit $de, implicit $hl
  //      %7:_(p0) = COPY $hl
  //      %4:_(p0) = COPY %7:_(p0)
  // In case the G_PTR_ADD offset is not an exact match, change G_PTR_ADD to use the HL(DE) and calculate a new offset from HL(DE) instead.

  // XXX should also do the same for "CALL _memcpyNN"

  bool changed = false;

  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  MachineIRBuilder MIB;
  MIB.setMF(MF);

  for (auto &MBB : MF) {
    Register hl_base_reg, de_base_reg;
    int64_t hl_base_offset = 0, de_base_offset = 0;

    std::map<Register, std::pair<Register, int64_t>> base_offsets;
    MachineInstr *LastLDI = nullptr;
    for (auto &MI : MBB) {
      // record all pointer offsets
      if (MI.getOpcode() == TargetOpcode::G_PTR_ADD) {
        Register dest = MI.getOperand(0).getReg();
        Register base = MI.getOperand(1).getReg();
        Register offset = MI.getOperand(2).getReg();
        auto Imm = getIConstantVRegValWithLookThrough(offset, MRI);
        if (Imm) {
          int64_t offset_value = Imm->Value.getZExtValue();
          base_offsets[dest] = {base, offset_value};
        }
      }

      if (MI.getOpcode() == TargetOpcode::COPY && MI.getOperand(0).getReg() == Z80::HL) {
        hl_base_reg = MI.getOperand(1).getReg();
        auto fit = base_offsets.find(hl_base_reg);
        if (fit != base_offsets.end()) {
          hl_base_reg = fit->second.first;
          hl_base_offset = fit->second.second;
        } else {
          hl_base_offset = 0;
        }
        LastLDI = nullptr;
        continue;
      }

      if (MI.getOpcode() == TargetOpcode::COPY && MI.getOperand(0).getReg() == Z80::DE) {
        de_base_reg = MI.getOperand(1).getReg();
        auto fit = base_offsets.find(de_base_reg);
        if (fit != base_offsets.end()) {
          de_base_reg = fit->second.first;
          de_base_offset = fit->second.second;
        } else {
          de_base_offset = 0;
        }
        LastLDI = nullptr;
        continue;
      }

      if (MI.getOpcode() == Z80::LDI16) { // load increment
        ++hl_base_offset;
        ++de_base_offset;
        LastLDI = &MI;
        continue;
      }
      if (MI.getOpcode() == Z80::LDD16) { // load decrement
        --hl_base_offset;
        --de_base_offset;
        LastLDI = &MI;
        continue;
      }
      if (MI.getOpcode() == Z80::CALL16 && isMemcpy(MI.getOperand(0))) { // load decrement
        auto it = MI.getIterator();
        // check if previous call is $BC=COPY
        if (it != MI.getParent()->begin()) {
          --it;
          if (it->getOpcode() == TargetOpcode::COPY && it->getOperand(0).getReg() == Z80::BC) {
            // make sure that bc value is an immediate, use it as offset
            if (auto Imm = getIConstantVRegValWithLookThrough(it->getOperand(1).getReg(), MRI)) {
              int64_t len = Imm->Value.getZExtValue();
              hl_base_offset += len;
              de_base_offset += len;
              LastLDI = &MI;
              continue;
            }
          }
        }
      }

      // if the instruction overwrites HL or DE, we give up -- the value is no longer directly available
      if (MI.definesRegister(Z80::HL)) {
        hl_base_reg = Register();
      }
      if (MI.definesRegister(Z80::DE)) {
        de_base_reg = Register();
      }

      if (!LastLDI)
        continue;

      if (MI.getOpcode() == TargetOpcode::G_PTR_ADD) {
        Register base = MI.getOperand(1).getReg();
        Register offset = MI.getOperand(2).getReg();
        if (auto Imm = getIConstantVRegValWithLookThrough(offset, MRI)) {
          int64_t offset_value = Imm->Value.getZExtValue();
          if (base == hl_base_reg && offset_value != hl_base_offset) {
            Register dest = MRI.createGenericVirtualRegister(LLT::pointer(0, 16));
            BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::COPY), dest).addReg(Z80::HL);
            Register off = MRI.createGenericVirtualRegister(LLT::scalar(16));
            BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::G_CONSTANT), off).addImm(offset_value - hl_base_offset);
            MI.getOperand(1).setReg(dest);
            MI.getOperand(2).setReg(off);
            changed = true;
          } else if (base == hl_base_reg && offset_value == hl_base_offset) {
            Register dest = MRI.createGenericVirtualRegister(LLT::pointer(0, 16));
            BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::COPY), dest).addReg(Z80::HL);
            MI.setDesc(TII->get(TargetOpcode::COPY));
            MI.getOperand(1).setReg(dest);
            MI.removeOperand(2);
            changed = true;
          } else if (base == de_base_reg && offset_value != de_base_offset) {
            Register dest = MRI.createGenericVirtualRegister(LLT::pointer(0, 16));
            BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::COPY), dest).addReg(Z80::DE);
            Register off = MRI.createGenericVirtualRegister(LLT::scalar(16));
            BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::G_CONSTANT), off).addImm(offset_value - de_base_offset);
            MI.getOperand(1).setReg(dest);
            MI.getOperand(2).setReg(off);
            changed = true;
          } else if (base == de_base_reg && offset_value == de_base_offset) {
            Register dest = MRI.createGenericVirtualRegister(LLT::pointer(0, 16));
            BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::COPY), dest).addReg(Z80::DE);
            MI.setDesc(TII->get(TargetOpcode::COPY));
            MI.getOperand(1).setReg(dest);
            MI.removeOperand(2);
            changed = true;
          }
        }
      }
    }
  }

  return changed;
}

char Z80LDIOptPass::ID = 0;

FunctionPass *llvm::createZ80LDIOptPass() {
  return new Z80LDIOptPass();
}

static RegisterPass<Z80LDIOptPass> X("z80-ldi-opt", "Z80 LDI address recovery",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

