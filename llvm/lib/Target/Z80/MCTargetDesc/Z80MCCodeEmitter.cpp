// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Z80BaseInfo.h"
#include "Z80FixupKinds.h"
#include "Z80MCTargetDesc.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {
class Z80MCCodeEmitter final : public MCCodeEmitter {
  const MCInstrInfo &MII;
  MCContext &Ctx;

  unsigned reg(const MCInst &MI, unsigned I) const {
    return Ctx.getRegisterInfo()->getEncodingValue(MI.getOperand(I).getReg());
  }

  static unsigned indexPrefix(MCRegister R) {
    switch (R.id()) {
    case Z80::IX:
    case Z80::UIX:
    case Z80::IXH:
    case Z80::IXL:
      return 0xDD;
    case Z80::IY:
    case Z80::UIY:
    case Z80::IYH:
    case Z80::IYL:
      return 0xFD;
    default:
      return 0;
    }
  }

  /// Append a little-endian operand, recording a relocation for a symbol.
  /// PC-relative operands are measured from the byte following the displacement.
  void emitValue(const MCOperand &Op, unsigned Size, MCFixupKind Kind,
                 bool PCRel, SMLoc Loc, SmallVectorImpl<char> &CB,
                 SmallVectorImpl<MCFixup> &Fixups) const {
    uint64_t Value = 0;
    if (Op.isImm()) {
      Value = Op.getImm();
      if ((PCRel || Kind == Z80::fixup_8_disp) && !isInt<8>(Value))
        Ctx.reportError(Loc, "Z80 displacement out of range");
      else if (!PCRel && Kind != Z80::fixup_8_disp &&
               !isIntN(Size * 8, Value) && !isUIntN(Size * 8, Value))
        Ctx.reportError(Loc, "Z80 immediate out of range");
    } else {
      const MCExpr *Expr = Op.getExpr();
      // ELF relocations are relative to the displacement byte; the CPU uses
      // the following instruction. Put this adjustment in the RELA addend.
      if (PCRel)
        Expr =
            MCBinaryExpr::createSub(Expr, MCConstantExpr::create(1, Ctx), Ctx);
      Fixups.push_back(MCFixup::create(CB.size(), Expr, Kind, PCRel));
    }
    for (unsigned I = 0; I != Size; ++I)
      CB.push_back(Value >> (I * 8));
  }

public:
  Z80MCCodeEmitter(const MCInstrInfo &MII, MCContext &Ctx)
      : MII(MII), Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};
} // namespace

void Z80MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                         SmallVectorImpl<char> &CB,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  const MCInstrDesc &D = MII.get(MI.getOpcode());
  if (D.isPseudo()) {
    Ctx.reportError(MI.getLoc(), "unexpanded Z80 pseudo instruction: " +
                                     MII.getName(MI.getOpcode()));
    return;
  }
  unsigned Flags = D.TSFlags;
  unsigned Opcode = (Flags & Z80II::OpcodeMask) >> Z80II::OpcodeShift;
  unsigned Index = 0;
  bool ED = Flags & Z80II::EDPre, CBPrefix = Flags & Z80II::CBPre;
  bool HasOffset = Flags & Z80II::HasOff;
  int OffsetOp = -1, ImmOp = -1;
  bool Relative = MI.getOpcode() == Z80::JR || MI.getOpcode() == Z80::JRCC ||
                  MI.getOpcode() == Z80::DJNZ;
  for (unsigned I = 0; I < 2 && I < D.getNumOperands(); ++I) {
    if (!(Flags & (Z80II::Idx0Pre << I)) || !MI.getOperand(I).isReg())
      continue;
    unsigned Prefix = indexPrefix(MI.getOperand(I).getReg());
    if (!Prefix)
      continue;
    if (Index && Index != Prefix) {
      Ctx.reportError(MI.getLoc(), "conflicting Z80 index registers");
      return;
    }
    Index = Prefix;
    ED = false;
    if (D.operands()[I].OperandType == MCOI::OPERAND_MEMORY) {
      if (HasOffset)
        OffsetOp = I + 1;
      HasOffset = true;
    }
  }

  if (CBPrefix) {
    if (D.operands()[0].OperandType == MCOI::OPERAND_MEMORY)
      Opcode |= 6;
    else
      Opcode |= reg(MI, 0);
    if (Opcode >= 0x40) {
      int64_t Bit = MI.getOperand(D.getNumOperands() - 1).getImm();
      if (!isUInt<3>(Bit)) {
        Ctx.reportError(MI.getLoc(), "Z80 bit number out of range");
        return;
      }
      Opcode |= Bit << 3;
    }
  } else {
    switch (MI.getOpcode()) {
    case Z80::LD8gg:
    case Z80::LD8xx:
    case Z80::LD8yy:
      Opcode |= reg(MI, 0) << 3 | reg(MI, 1);
      break;
    case Z80::LD8ri:
    case Z80::LD8gp:
    case Z80::LD8go:
    case Z80::INC8r:
    case Z80::DEC8r:
    case Z80::IN8gm:
    case Z80::IN16gp:
    case Z80::IN24gp:
    case Z80::OUT16pg:
    case Z80::OUT24pg:
      Opcode |= reg(MI, 0) << 3;
      break;
    case Z80::INC8p:
    case Z80::DEC8p:
    case Z80::INC8o:
    case Z80::DEC8o:
      Opcode |= 6 << 3;
      break;
    case Z80::LD8ap:
      Opcode = reg(MI, 1) < 2 ? 0x0A | (reg(MI, 1) << 4) : 0x7E;
      break;
    case Z80::LD8pa:
      Opcode = reg(MI, 0) < 2 ? 0x02 | (reg(MI, 0) << 4) : 0x77;
      break;
    case Z80::LD8pg:
      Opcode |= reg(MI, 1);
      break;
    case Z80::LD8og:
      Opcode |= reg(MI, 2);
      break;
    case Z80::LD16ri:
    case Z80::LD24ri:
    case Z80::LD16gm:
    case Z80::LD24gm:
    case Z80::INC16r:
    case Z80::DEC16r:
    case Z80::INC24r:
    case Z80::DEC24r:
    case Z80::PUSH16r:
    case Z80::POP16r:
    case Z80::PUSH24r:
    case Z80::POP24r:
    case Z80::MLT16r:
    case Z80::ADC16ao:
    case Z80::SBC16ao:
    case Z80::ADC24ao:
    case Z80::SBC24ao:
      Opcode |= reg(MI, 0) << 4;
      break;
    case Z80::LD16mg:
    case Z80::LD24mg:
      Opcode |= reg(MI, 1) << 4;
      break;
    case Z80::ADD16ao:
    case Z80::ADD24ao:
      Opcode |= reg(MI, 2) << 4;
      break;
    case Z80::OUT8mg:
      Opcode |= reg(MI, 1) << 3;
      break;
    case Z80::ADD8ar:
    case Z80::ADC8ar:
    case Z80::SUB8ar:
    case Z80::SBC8ar:
    case Z80::AND8ar:
    case Z80::XOR8ar:
    case Z80::OR8ar:
    case Z80::CP8ar:
      Opcode |= reg(MI, 0);
      break;
    case Z80::TST8ag:
      Opcode |= reg(MI, 0) << 3;
      break;
    case Z80::JRCC:
    case Z80::JP16CC:
    case Z80::JP24CC:
    case Z80::CALL16CC:
    case Z80::CALL24CC:
    case Z80::RET16CC:
    case Z80::RET24CC: {
      int64_t CC = MI.getOperand(D.getNumOperands() - 1).getImm();
      if (!isUInt<3>(CC) || (Relative && CC > 3)) {
        Ctx.reportError(MI.getLoc(), "invalid Z80 branch condition");
        return;
      }
      Opcode |= CC << 3;
      break;
    }
    case Z80::RST16:
    case Z80::RST24: {
      int64_t Target = MI.getOperand(0).getImm();
      if (Target < 0 || Target > 0x38 || (Target & 7)) {
        Ctx.reportError(MI.getLoc(), "invalid Z80 restart address");
        return;
      }
      Opcode |= Target;
      break;
    }
    case Z80::IM: {
      int64_t Mode = MI.getOperand(0).getImm();
      if (Mode < 0 || Mode > 2) {
        Ctx.reportError(MI.getLoc(), "invalid Z80 interrupt mode");
        return;
      }
      Opcode |= Mode ? (Mode + 1) << 3 : 0;
      break;
    }
    case Z80::LD16rp:
    case Z80::LD24rp:
    case Z80::LD16pr:
    case Z80::LD24pr:
    case Z80::LD16ro:
    case Z80::LD24ro:
    case Z80::LD16or:
    case Z80::LD24or: {
      bool Load = D.mayLoad();
      unsigned RegOp = Load ? 0 : (Flags & Z80II::HasOff) ? 2 : 1;
      unsigned PairIndex = indexPrefix(MI.getOperand(RegOp).getReg());
      if (PairIndex) {
        // IX and IY swap roles under FD; ED uses the DD register ordering.
        bool SameIndex = PairIndex == (Index ? Index : 0xDD);
        Opcode = Load ? (SameIndex ? 0x37 : 0x31) : (SameIndex ? 0x3F : 0x3E);
      } else
        Opcode |= reg(MI, RegOp) << 4;
      break;
    }
    case Z80::LEA16ro:
    case Z80::LEA24ro: {
      unsigned Dst = indexPrefix(MI.getOperand(0).getReg());
      unsigned Src = indexPrefix(MI.getOperand(1).getReg());
      if (Dst)
        Opcode = Dst == Src ? (Dst == 0xDD ? 0x32 : 0x33)
                            : (Dst == 0xDD ? 0x54 : 0x55);
      else
        Opcode |= reg(MI, 0) << 4 | (Src == 0xFD);
      OffsetOp = 2;
      break;
    }
    case Z80::PEA16o:
    case Z80::PEA24o:
      Opcode += indexPrefix(MI.getOperand(0).getReg()) == 0xFD;
      OffsetOp = 1;
      break;
    default:
      // Remaining real instructions have fixed opcodes. Their operands select
      // an index prefix or supply an immediate/address after the opcode.
      break;
    }
  }

  if (Flags & Z80II::HasImm) {
    for (unsigned I = 0; I < D.getNumOperands(); ++I)
      if (!MI.getOperand(I).isReg() && int(I) != OffsetOp) {
        ImmOp = I;
        break;
      }
    assert(ImmOp >= 0 && "missing immediate operand");
  }
  unsigned Mode = Flags & Z80II::ModeMask;
  bool ADL = STI.hasFeature(Z80::Mode24Bit);
  unsigned ExplicitPrefix = MI.getFlags() & Z80II::ExplicitPrefixMask;
  if (ExplicitPrefix)
    CB.push_back(ExplicitPrefix);
  else if (Mode == Z80II::Z80Mode && ADL)
    CB.push_back(0x40);
  else if (Mode == Z80II::EZ80Mode && !ADL)
    CB.push_back(0x5B);
  if (Index)
    CB.push_back(Index);
  if (CBPrefix)
    CB.push_back(0xCB);
  if (ED)
    CB.push_back(0xED);

  auto EmitOffset = [&] {
    if (Relative)
      emitValue(MI.getOperand(0), 1, Z80::fixup_8_pcrel, true, MI.getLoc(), CB,
                Fixups);
    else if (OffsetOp >= 0)
      emitValue(MI.getOperand(OffsetOp), 1, Z80::fixup_8_disp, false,
                MI.getLoc(), CB, Fixups);
    else
      CB.push_back(0);
  };
  if (CBPrefix && HasOffset)
    EmitOffset();
  CB.push_back(Opcode);
  if (!CBPrefix && HasOffset)
    EmitOffset();
  if (ImmOp >= 0) {
    unsigned Size = Mode == Z80II::AnyMode    ? 1
                    : Mode == Z80II::Z80Mode  ? 2
                    : Mode == Z80II::EZ80Mode ? 3
                    : ADL                     ? 3
                                              : 2;
    if (ExplicitPrefix && Size != 1)
      Size = ExplicitPrefix & 0x10 ? 3 : 2;
    emitValue(MI.getOperand(ImmOp), Size,
              Size == 3 ? MCFixupKind(Z80::fixup_24)
                        : MCFixup::getDataKindForSize(Size),
              false, MI.getLoc(), CB, Fixups);
  }
}

MCCodeEmitter *llvm::createZ80MCCodeEmitter(const MCInstrInfo &MII,
                                            MCContext &Ctx) {
  return new Z80MCCodeEmitter(MII, Ctx);
}
