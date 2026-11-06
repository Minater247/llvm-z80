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
    /* Carry distinguishes negative from positive overflow. Subtracting A
       from itself with carry produces 0xff or 0, which XOR maps to the
       signed byte limits without loading a separate constant register. */
    Emit(Z80::SBC8ar, {MCOperand::createReg(Z80::A)});
    Emit(Z80::XOR8ai, {MCOperand::createImm(127)});
    OutStreamer->emitLabel(Done);
    return;
  }
  if (Opc == Z80::BANK_ADD16) {
    auto Main = [](MCRegister R) -> MCRegister {
      return R == Z80::HL_ALT   ? Z80::HL
             : R == Z80::DE_ALT ? Z80::DE
             : R == Z80::BC_ALT ? Z80::BC
                                : R;
    };
    auto Emit = [&](unsigned Code, ArrayRef<MCRegister> Regs = {}) {
      MCInst Inst;
      Inst.setOpcode(Code);
      for (MCRegister R : Regs)
        Inst.addOperand(MCOperand::createReg(R));
      EmitToStreamer(*OutStreamer, Inst);
    };
    bool CFI =
        MF->needsFrameMoves() && !getSubtarget().getFrameLowering()->hasFP(*MF);
    auto Push = [&](MCRegister R) {
      Emit(Z80::PUSH16r, {R});
      if (CFI)
        OutStreamer->emitCFIAdjustCfaOffset(2);
    };
    auto Pop = [&](MCRegister R) {
      Emit(Z80::POP16r, {R});
      if (CFI)
        OutStreamer->emitCFIAdjustCfaOffset(-2);
    };
    auto Load = [&](MCRegister Dst, MCRegister Src) {
      Emit(Z80::EXX16);
      Push(Main(Src));
      Emit(Z80::EXX16);
      Pop(Dst);
    };
    auto Save = [&](MCRegister Dst, MCRegister Src) {
      Push(Src);
      Emit(Z80::EXX16);
      Pop(Main(Dst));
      Emit(Z80::EXX16);
    };
    auto Add = [&](MCRegister Dst, MCRegister Src) {
      if (Dst == Z80::DE) {
        Emit(Z80::EX16DE);
        if (Src == Z80::DE)
          Emit(Z80::ADD16aa, {Z80::HL, Z80::HL});
        else
          Emit(Z80::ADD16ao, {Z80::HL, Z80::HL, Src});
        Emit(Z80::EX16DE);
      } else if (Dst == Z80::BC) {
        Push(Z80::HL);
        Emit(Z80::LD8gg, {Z80::L, Z80::C});
        Emit(Z80::LD8gg, {Z80::H, Z80::B});
        Emit(Z80::ADD16ao, {Z80::HL, Z80::HL, Src});
        Emit(Z80::LD8gg, {Z80::C, Z80::L});
        Emit(Z80::LD8gg, {Z80::B, Z80::H});
        Pop(Z80::HL);
      } else
        Emit(Z80::ADD16ao, {Dst, Dst, Src});
    };
    MCRegister Dst = MI->getOperand(0).getReg(),
               Src = MI->getOperand(2).getReg();
    bool DstAlt = Main(Dst) != Dst, SrcAlt = Main(Src) != Src;
    if (DstAlt && SrcAlt) {
      Emit(Z80::EXX16);
      Add(Main(Dst), Main(Src));
      Emit(Z80::EXX16);
    } else if (DstAlt) {
      Push(Z80::HL);
      Load(Z80::HL, Dst);
      Add(Z80::HL, MI->getOperand(2).getReg());
      Save(Dst, Z80::HL);
      Pop(Z80::HL);
    } else if (SrcAlt) {
      MCRegister Scratch = Dst == Z80::BC ? Z80::DE : Z80::BC;
      Push(Scratch);
      Load(Scratch, Src);
      Add(Dst, Scratch);
      Pop(Scratch);
    } else
      Add(Dst, Src);
    return;
  }
  if (Opc == Z80::BANK_LD16ri || Opc == Z80::BANK_LD16rm ||
      Opc == Z80::BANK_LD16mr || Opc == Z80::BANK_INC16 ||
      Opc == Z80::BANK_DEC16 || Opc == Z80::BANK_INCL16 ||
      Opc == Z80::BANK_DECL16 || Opc == Z80::BANK_LD8ap ||
      Opc == Z80::BANK_LD8pa || Opc == Z80::ALT_COPY16 ||
      Opc == Z80::BANK_BIT8pb || Opc == Z80::BANK_LD8pi) {
    auto Main = [](MCRegister R) -> MCRegister {
      return R == Z80::HL_ALT ? Z80::HL : R == Z80::DE_ALT ? Z80::DE : Z80::BC;
    };
    auto Emit = [&](unsigned Code, ArrayRef<MCOperand> Ops = {}) {
      MCInst Inst;
      Inst.setOpcode(Code);
      for (const auto &Op : Ops)
        Inst.addOperand(Op);
      EmitToStreamer(*OutStreamer, Inst);
    };
    auto Reg = [](MCRegister R) { return MCOperand::createReg(R); };
    Emit(Z80::EXX16);
    if (Opc == Z80::ALT_COPY16) {
      auto Dst = Main(MI->getOperand(0).getReg());
      auto Src = Main(MI->getOperand(1).getReg());
      const auto &TRI = *getSubtarget().getRegisterInfo();
      for (unsigned Sub : {Z80::sub_low, Z80::sub_high})
        Emit(Z80::LD8gg,
             {Reg(TRI.getSubReg(Dst, Sub)), Reg(TRI.getSubReg(Src, Sub))});
    } else {
      unsigned Op = Opc == Z80::BANK_LD8ap || Opc == Z80::BANK_LD16mr ? 1 : 0;
      auto Pair = Reg(Main(MI->getOperand(Op).getReg()));
      if (Opc == Z80::BANK_LD16rm || Opc == Z80::BANK_LD16mr) {
        bool Load = Opc == Z80::BANK_LD16rm;
        bool HL = Main(MI->getOperand(Op).getReg()) == Z80::HL;
        unsigned Code = Load ? (HL ? Z80::LD16am : Z80::LD16gm)
                             : (HL ? Z80::LD16ma : Z80::LD16mg);
        Z80MCInstLower Lower(*MF, *this);
        auto Address = *Lower.LowerMachineOperand(MI, MI->getOperand(1 - Op));
        if (Load)
          Emit(Code, {Pair, Address});
        else
          Emit(Code, {Address, Pair});
      } else if (Opc == Z80::BANK_BIT8pb || Opc == Z80::BANK_LD8pi) {
        Emit(Opc == Z80::BANK_BIT8pb ? Z80::BIT8pb : Z80::LD8pi,
             {Pair, MCOperand::createImm(MI->getOperand(1).getImm())});
      } else if (Opc == Z80::BANK_LD16ri) {
        Z80MCInstLower Lower(*MF, *this);
        Emit(Z80::LD16ri,
             {Pair, *Lower.LowerMachineOperand(MI, MI->getOperand(1))});
      } else if (Opc == Z80::BANK_INCL16 || Opc == Z80::BANK_DECL16) {
        const auto &TRI = *getSubtarget().getRegisterInfo();
        auto Low = Reg(TRI.getSubReg(Main(MI->getOperand(Op).getReg()),
                                    Z80::sub_low));
        Emit(Opc == Z80::BANK_INCL16 ? Z80::INC8r : Z80::DEC8r, {Low, Low});
      } else if (Opc == Z80::BANK_INC16 || Opc == Z80::BANK_DEC16)
        Emit(Opc == Z80::BANK_INC16 ? Z80::INC16r : Z80::DEC16r, {Pair, Pair});
      else if (Opc == Z80::BANK_LD8ap)
        Emit(Z80::LD8ap, {Reg(Z80::A), Pair});
      else
        Emit(Z80::LD8pa, {Pair, Reg(Z80::A)});
    }
    Emit(Z80::EXX16);
    return;
  }
  if (Opc == Z80::ALT_SWAP_SAVE8 || Opc == Z80::ALT_SWAP_LOAD8) {
    bool Load = Opc == Z80::ALT_SWAP_LOAD8;
    MCRegister Main = MI->getOperand(Load ? 0 : 1).getReg();
    auto Exchange = [&] {
      MCInst Inst;
      Inst.setOpcode(Z80::EXAF);
      EmitToStreamer(*OutStreamer, Inst);
    };
    if (Load)
      Exchange();
    if (Main != Z80::A) {
      MCInst Inst;
      Inst.setOpcode(Z80::I8RegClass.contains(Main)
                         ? (Z80::X8RegClass.contains(Main) ? Z80::LD8xx
                                                         : Z80::LD8yy)
                         : Z80::LD8gg);
      Inst.addOperand(MCOperand::createReg(Load ? Main : MCRegister(Z80::A)));
      Inst.addOperand(MCOperand::createReg(Load ? MCRegister(Z80::A) : Main));
      EmitToStreamer(*OutStreamer, Inst);
    }
    if (!Load)
      Exchange();
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
    } else if (Alt != Z80::A_ALT) {
      MCRegister Bank = Alt == Z80::L_ALT ? Z80::L
                        : Alt == Z80::H_ALT ? Z80::H
                        : Alt == Z80::E_ALT ? Z80::E
                        : Alt == Z80::D_ALT ? Z80::D
                        : Alt == Z80::C_ALT ? Z80::C : Z80::B;
      unsigned Move = Z80::I8RegClass.contains(Main)
                          ? (Z80::X8RegClass.contains(Main) ? Z80::LD8xx
                                                          : Z80::LD8yy)
                          : Z80::LD8gg;
      unsigned Exchange = Wide ? Z80::EXX24 : Z80::EXX16;
      /* A stays visible through EXX. Preserve it when it bridges a transfer
         between a primary byte and an alternate byte. LD and EXX preserve F. */
      if (Main != Z80::A) {
        Push(Z80::AF);
        if (Save)
          Emit(Move, {Z80::A, Main});
      }
      Emit(Exchange);
      Emit(Z80::LD8gg, {Load ? MCRegister(Z80::A) : Bank,
                       Load ? Bank : MCRegister(Z80::A)});
      Emit(Exchange);
      if (Main != Z80::A) {
        if (Load)
          Emit(Move, {Main, Z80::A});
        Pop(Z80::AF);
      }
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
