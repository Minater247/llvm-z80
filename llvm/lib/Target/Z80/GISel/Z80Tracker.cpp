
#include "Z80Tracker.h"

using namespace llvm;

void Z80Tracker::process(MachineInstr& MI)
{
  if (MI.getOpcode() == TargetOpcode::G_CONSTANT) {
    auto& MO0 = MI.getOperand(0);
    auto& MO1 = MI.getOperand(1);
    Register Dst = MO0.getReg();
    clobber(Dst, false);
    auto& DstRE = getReg(Dst);
    auto Imm = extractImmediate(MO1);
    if (Imm) {
      DstRE.setImm(MI, *Imm);
    }
    return;
  } else if (MI.getOpcode() == Z80::LD16ri) {
    auto& MO0 = MI.getOperand(0);
    auto& MO1 = MI.getOperand(1);
    Register Dst = MO0.getReg();
    clobber(Dst, false);
    auto& DstRE = getReg(Dst);
    auto Imm = extractImmediate(MO1);
    if (Imm) {
      DstRE.setImm(MI, *Imm);
    }
    return;
  } else if (MI.getOpcode() == TargetOpcode::G_PTR_ADD) {
    auto& MO0 = MI.getOperand(0);
    auto& MO1 = MI.getOperand(1);
    auto& MO2 = MI.getOperand(2);
    clobber(MO0.getReg(), false);
    auto& DstRE = getReg(MO0.getReg());
    auto& SrcRE = getReg(MO1.getReg());
    auto& OffRE = getReg(MO2.getReg());
    SrcRE.use(MI);
    OffRE.use(MI);
    if (!SrcRE.isSet() && OffRE.isImm()) {
      DstRE.def(MI, SrcRE.getReg(), OffRE.getImm());
    }
    if (SrcRE.isImm() && OffRE.isImm()) {
      DstRE.setImm(MI, SrcRE.getImm() + OffRE.getImm());
    }
    return;
  } else if (MI.getOpcode() == Z80::INC16r && MI.getOperand(0).getReg() ==  MI.getOperand(1).getReg()) {
    auto& RE = getReg(MI.getOperand(0).getReg());
    RE.inc(MI);
    return;
  } else if (MI.getOpcode() == Z80::DEC16r && MI.getOperand(0).getReg() ==  MI.getOperand(1).getReg()) {
    auto& RE = getReg(MI.getOperand(0).getReg());
    RE.dec(MI);
    return;
  } else if (MI.getOpcode() == Z80::ADD16ao) {
    // %287:a16 = ADD16ao %17:i16(tied-def 0), %368:o16, implicit-def $f
    auto& MO0 = MI.getOperand(0);
    auto& MO1 = MI.getOperand(1);
    auto& MO2 = MI.getOperand(2);
    clobber(MO0.getReg(), false);
    clobber(Z80::F, true);
    auto& DstRE = getReg(MO0.getReg());
    auto& SrcRE = getReg(MO1.getReg());
    auto& OffRE = getReg(MO2.getReg());
    SrcRE.use(MI);
    OffRE.use(MI);
    Optional<int64_t> Imm;
    if (OffRE.isImm()) {
      Imm = OffRE.getImm();
    } else {
      Imm = extractImmediate(MO2);
    }
    if (Imm) {
      if (SrcRE.isImm()) {
        DstRE.setImm(MI,  SrcRE.getImm() + *Imm);
      } else if (SrcRE.isRegOff()) {
        DstRE.def(MI, SrcRE.getRegOff().first, SrcRE.getRegOff().second + *Imm);
      } else if (!SrcRE.isSet()) {
        DstRE.def(MI, SrcRE.getReg(), *Imm);
      }
    }
    return;
  } else if (MI.getOpcode() == TargetOpcode::COPY) {
    auto& MO0 = MI.getOperand(0);
    auto& MO1 = MI.getOperand(1);
    Register Dst = MO0.getReg();
    Register Src = MO1.getReg();
    auto& DstRE = getReg(Dst);
    auto& SrcRE = getReg(Src);
    SrcRE.use(MI);
    clobber(Dst, false);
    if (SrcRE.isRegOff() && SrcRE.getRegOff().first != Dst) {
      DstRE.def(MI, SrcRE.getRegOff().first, SrcRE.getRegOff().second);
    } else if (SrcRE.isImm()) {
      DstRE.setImm(MI, SrcRE.getImm());
    } else {
      if (Src.isVirtual()) {
        // try to find cross-branch constants
        if (auto Imm = getIConstantVRegValWithLookThrough(Src, MRI)) {
          int64_t value = Imm->Value.getSExtValue();
          DstRE.setImm(MI, value);
        } else {
          DstRE.def(MI, Src, 0);
        }
      } else {
        DstRE.def(MI, Src, 0);
      }
    }
    return;
  } else if (MI.getOpcode() == Z80::LDI16) {
    clobber(Z80::BC, false);
    clobber(Z80::HL, false);
    clobber(Z80::DE, false);
    auto& bcRE = getReg(Z80::BC);
    bcRE.dec(MI);
    auto& hlRE = getReg(Z80::HL);
    hlRE.inc(MI);
    auto& deRE = getReg(Z80::DE);
    deRE.inc(MI);
    return;
  } else if (MI.getOpcode() == Z80::LDD16) {
    clobber(Z80::BC, false);
    clobber(Z80::HL, false);
    clobber(Z80::DE, false);
    auto& bcRE = getReg(Z80::BC);
    bcRE.dec(MI);
    auto& hlRE = getReg(Z80::HL);
    hlRE.dec(MI);
    auto& deRE = getReg(Z80::DE);
    deRE.dec(MI);
    return;
  } else if (MI.getOpcode() == Z80::EX16DE) {
    clobber(Z80::HL, false);
    clobber(Z80::DE, false);
    auto& hlRE = getReg(Z80::HL);
    auto& deRE = getReg(Z80::DE);
    hlRE.ex(MI, deRE);
    return;
  } else if (MI.getOpcode() == Z80::CALL16 && isMemcpy(MI.getOperand(0))) {
    clobber(Z80::BC, false);
    clobber(Z80::HL, false);
    clobber(Z80::DE, false);
    auto& bcRE = getReg(Z80::BC);
    auto& hlRE = getReg(Z80::HL);
    auto& deRE = getReg(Z80::DE);
    if (bcRE.isImm()) {
      hlRE.add(MI, bcRE.getImm());
      deRE.add(MI, bcRE.getImm());
    } else {
      hlRE.reset();
      deRE.reset();
    }
    bcRE.setImm(MI, 0);
    return;
  } else if (MI.getOpcode() == Z80::LDIR16) {
    clobber(Z80::BC, false);
    clobber(Z80::HL, false);
    clobber(Z80::DE, false);
    auto& bcRE = getReg(Z80::BC);
    auto& hlRE = getReg(Z80::HL);
    auto& deRE = getReg(Z80::DE);
    if (bcRE.isImm()) {
      hlRE.add(MI, bcRE.getImm());
      deRE.add(MI, bcRE.getImm());
    } else {
      hlRE.reset();
      deRE.reset();
    }
    bcRE.setImm(MI, 0);
    return;
  } else if (MI.getOpcode() == Z80::LDDR16) {
    clobber(Z80::BC, false);
    clobber(Z80::HL, false);
    clobber(Z80::DE, false);
    auto& bcRE = getReg(Z80::BC);
    auto& hlRE = getReg(Z80::HL);
    auto& deRE = getReg(Z80::DE);
    if (bcRE.isImm()) {
      hlRE.sub(MI, bcRE.getImm());
      deRE.sub(MI, bcRE.getImm());
    } else {
      hlRE.reset();
      deRE.reset();
    }
    bcRE.setImm(MI, 0);
    return;
  } else if (MI.getOpcode() == Z80::ADD16aa) {
    auto& MO0 = MI.getOperand(0);
    auto& MO1 = MI.getOperand(1);
    Register Dst = MO0.getReg();
    Register Src = MO1.getReg();
    auto& DstRE = getReg(Dst);
    auto& SrcRE = getReg(Src);
    clobber(Z80::F);
    if (DstRE.isRegOff() && SrcRE.isImm()) {
      clobber(Dst, false);
      DstRE.def(MI, DstRE.getRegOff().first, DstRE.getRegOff().second + SrcRE.getImm());
    } else {
      clobber(Dst, true);
    }
    SrcRE.use(MI);
    return;
  }

  // clobber defined registers
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg()) {
      continue;
    }
    Register Reg = MO.getReg();
    assert(Reg);
    // Reg gets defined. Clobber anything that uses it.
    if (MO.isDef()) {
      clobber(MO.getReg());
    } else if (MO.isUse() && Regs.count(Reg) != 0) {
        getReg(Reg).use(MI);
    }
  }
}

bool Z80Tracker::isMemcpy(const MachineOperand& Op)
{
  if (!Op.isSymbol())
      return false;
  StringRef FuncName = Op.getSymbolName();
  if (!FuncName.startswith("_memcpy"))
      return false;

  // Check if the remaining part is a valid number
  StringRef Suffix = FuncName.drop_front(7); // Drop "_memcpy"
  if (Suffix.empty())
      return true;

  return Suffix.size() == 2 && Suffix.find_first_not_of("0123456789") == StringRef::npos;
}

Optional<int64_t> Z80Tracker::extractImmediate(MachineOperand& MO) {
  if (MO.isImm()) {
    return MO.getImm();
  } else if (MO.isCImm()) {
    return MO.getCImm()->getSExtValue();
  } else if (MO.isReg() && MO.getReg().isVirtual()) {
    Register Reg = MO.getReg();
    if (auto Imm = getIConstantVRegValWithLookThrough(MO.getReg(), MRI)) {
      return Imm->Value.getSExtValue();
    }
    // This fails because of invalid LLT type of the Reg. What the heck?
    //if (MachineInstr *DefMI = getDefIgnoringCopies(Reg, MRI)) { }
    if (MachineInstr *DefMI = MRI.getVRegDef(Reg)) {
      if (DefMI->getOpcode() == Z80::LD16ri) {
        return extractImmediate(DefMI->getOperand(1));
      }
    }
    return None;
  } else {
    return None;
  }
}

Z80Tracker::Z80Tracker(MachineBasicBlock& MBB)
    : MBB(MBB),
      TRI(MBB.getParent()->getSubtarget().getRegisterInfo()),
      TII(MBB.getParent()->getSubtarget().getInstrInfo()),
      MRI(MBB.getParent()->getRegInfo()) {
}

