//===-- Z80MCInstLower.cpp - Convert Z80 MachineInstr to an MCInst --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Z80 MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "Z80AsmPrinter.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80Subtarget.h"
#include <optional>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "z80-mclower"

namespace {
/// Z80MCInstLower - This class is used to lower a MachineInstr into an MCInst.
class Z80MCInstLower {
  MCContext &Ctx;
  Z80AsmPrinter &AsmPrinter;

public:
  Z80MCInstLower(const MachineFunction &MF, Z80AsmPrinter &AP);

  std::optional<MCOperand> LowerMachineOperand(const MachineInstr *MI,
                                               const MachineOperand &MO) const;

  MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
  MCOperand LowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
};
}

Z80MCInstLower::Z80MCInstLower(const MachineFunction &MF, Z80AsmPrinter &AP)
    : Ctx(MF.getContext()), AsmPrinter(AP) {}

/// GetGlobalAddressSymbol - Lower an MO_GlobalAddress operand to an MCSymbol.
MCSymbol *
Z80MCInstLower::GetGlobalAddressSymbol(const MachineOperand &MO) const {
  assert(!MO.getTargetFlags() && "Unknown target flag on GV operand");
  return AsmPrinter.getSymbol(MO.getGlobal());
}

/// GetExternalSymbolSymbol - Lower an MO_ExternalSymbol operand to an MCSymbol.
MCSymbol *
Z80MCInstLower::GetExternalSymbolSymbol(const MachineOperand &MO) const {
  assert(!MO.getTargetFlags() && "Unknown target flag on GV operand");
  return AsmPrinter.GetExternalSymbolSymbol(MO.getSymbolName());
}

MCOperand Z80MCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                             MCSymbol *Sym) const {
  assert(!MO.getTargetFlags() && "Unknown target flag on GV operand");
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

std::optional<MCOperand>
Z80MCInstLower::LowerMachineOperand(const MachineInstr *MI,
                                    const MachineOperand &MO) const {
  switch (MO.getType()) {
  default:
    LLVM_DEBUG(MI->dump());
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_CImmediate:
    return MCOperand::createImm(MO.getCImm()->getSExtValue());
  case MachineOperand::MO_MachineBasicBlock:
    return MCOperand::createExpr(MCSymbolRefExpr::create(
                       MO.getMBB()->getSymbol(), Ctx));
  case MachineOperand::MO_GlobalAddress:
    return LowerSymbolOperand(MO, GetGlobalAddressSymbol(MO));
  case MachineOperand::MO_ExternalSymbol:
    return LowerSymbolOperand(MO, GetExternalSymbolSymbol(MO));
  case MachineOperand::MO_JumpTableIndex:
    return LowerSymbolOperand(MO, AsmPrinter.GetJTISymbol(MO.getIndex()));
  case MachineOperand::MO_RegisterMask:
    return std::nullopt; // Ignore call clobbers.
  }
}

void Z80MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  MCOperand MCOp;
  for (const MachineOperand &MO : MI->operands())
    if (auto PossibleMCOp = LowerMachineOperand(MI, MO))
      OutMI.addOperand(*PossibleMCOp);
}

void Z80AsmPrinter::emitInstruction(const MachineInstr *MI) {
  unsigned Opc = MI->getOpcode();
  if (Opc == Z80::SADDSAT8ar || Opc == Z80::SADDSAT8ai ||
      Opc == Z80::SADDSAT8ap) {
    auto Emit = [&](unsigned Opcode, ArrayRef<MCOperand> Operands) {
      MCInst Inst;
      Inst.setOpcode(Opcode);
      for (auto Operand : Operands)
        Inst.addOperand(Operand);
      EmitToStreamer(*OutStreamer, Inst);
    };
    bool Immediate = Opc == Z80::SADDSAT8ai;
    Emit(Immediate ? Z80::ADD8ai
                   : Opc == Z80::SADDSAT8ap ? Z80::ADD8ap : Z80::ADD8ar,
         {Immediate ? MCOperand::createImm(MI->getOperand(0).getImm())
                    : MCOperand::createReg(MI->getOperand(0).getReg())});
    auto *Done = OutContext.createTempSymbol("sat");
    Emit(getSubtarget().is24Bit() ? Z80::JP24CC : Z80::JP16CC,
         {MCOperand::createExpr(MCSymbolRefExpr::create(Done, OutContext)),
          MCOperand::createImm(Z80::COND_PO)});
    // Overflow with carry is negative overflow; without carry it is positive.
    Emit(Z80::LD8ri, {MCOperand::createReg(Z80::A), MCOperand::createImm(127)});
    Emit(Z80::ADC8ai, {MCOperand::createImm(0)});
    OutStreamer->emitLabel(Done);
    return;
  }
  bool Save = Opc == Z80::ALT_SAVE8 || Opc == Z80::ALT_SAVE16 || Opc == Z80::ALT_SAVE24;
  bool Load = Opc == Z80::ALT_LOAD8 || Opc == Z80::ALT_LOAD16 || Opc == Z80::ALT_LOAD24;
  if (Save || Load) {
    MCRegister Main = MI->getOperand(Load ? 0 : 1).getReg();
    MCRegister Alt = MI->getOperand(Load ? 1 : 0).getReg();
    bool Wide = getSubtarget().is24Bit();
    bool Byte = Opc == Z80::ALT_SAVE8 || Opc == Z80::ALT_LOAD8;
    bool CFI =
        MF->needsFrameMoves() && !getSubtarget().getFrameLowering()->hasFP(*MF);
    auto Emit = [&](unsigned Op, ArrayRef<MCRegister> Regs = {}) {
      MCInst Inst;
      Inst.setOpcode(Op);
      for (MCRegister R : Regs)
        Inst.addOperand(MCOperand::createReg(R));
      EmitToStreamer(*OutStreamer, Inst);
    };
    auto Push = [&](MCRegister R) {
      if (R == Z80::AF)
        Emit(Wide ? Z80::PUSH24AF : Z80::PUSH16AF);
      else
        Emit(Wide ? Z80::PUSH24r : Z80::PUSH16r, {R});
      if (CFI)
        OutStreamer->emitCFIAdjustCfaOffset(Wide ? 3 : 2);
    };
    auto Pop = [&](MCRegister R) {
      if (R == Z80::AF)
        Emit(Wide ? Z80::POP24AF : Z80::POP16AF);
      else
        Emit(Wide ? Z80::POP24r : Z80::POP16r, {R});
      if (CFI)
        OutStreamer->emitCFIAdjustCfaOffset(Wide ? -3 : -2);
    };
    if (!Byte) {
      MCRegister Bank = Alt == Z80::HL_ALT    ? Z80::HL
                        : Alt == Z80::DE_ALT  ? Z80::DE
                        : Alt == Z80::BC_ALT  ? Z80::BC
                        : Alt == Z80::UHL_ALT ? Z80::UHL
                        : Alt == Z80::UDE_ALT ? Z80::UDE
                                              : Z80::UBC;
      unsigned Exchange = Wide ? Z80::EXX24 : Z80::EXX16;
      if (Load)
        Emit(Exchange);
      Push(Load ? Bank : Main);
      Emit(Exchange);
      Pop(Load ? Main : Bank);
      if (Save)
        Emit(Exchange);
    } else if (Main == Z80::A && Save) {
      Push(Z80::AF);
      Emit(Z80::EXAF);
      Pop(Z80::AF);
      Emit(Z80::EXAF);
    } else if (Main == Z80::A) {
      // POP AF would corrupt the primary flags with the alternate flags.
      // Use a preserved byte of BC instead, retaining both banks' flags.
      MCRegister BC = Wide ? Z80::UBC : Z80::BC;
      Push(BC);
      Emit(Z80::EXAF);
      Emit(Z80::LD8gg, {Z80::B, Z80::A});
      Emit(Z80::EXAF);
      Emit(Z80::LD8gg, {Z80::A, Z80::B});
      Pop(BC);
    } else {
      unsigned Move =
          Z80::I8RegClass.contains(Main)
              ? (Z80::X8RegClass.contains(Main) ? Z80::LD8xx : Z80::LD8yy)
              : Z80::LD8gg;
      Emit(Z80::EXAF);
      Emit(Move, {Load ? Main : MCRegister(Z80::A),
                  Load ? MCRegister(Z80::A) : Main});
      Emit(Z80::EXAF);
    }
    return;
  }
  Z80MCInstLower MCInstLowering(*MF, *this);

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}
