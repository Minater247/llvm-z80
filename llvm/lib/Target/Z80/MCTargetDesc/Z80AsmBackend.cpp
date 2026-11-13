// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Z80FixupKinds.h"
#include "Z80MCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class Z80AsmBackend final : public MCAsmBackend {
  uint8_t OSABI;

public:
  explicit Z80AsmBackend(uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createZ80ELFObjectWriter(OSABI);
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    unsigned Type = StringSwitch<unsigned>(Name)
#define ELF_RELOC(Name, Value) .Case(#Name, Value)
#include "llvm/BinaryFormat/ELFRelocs/Z80.def"
#undef ELF_RELOC
                        .Case("BFD_RELOC_NONE", ELF::R_Z80_NONE)
                        .Case("BFD_RELOC_8", ELF::R_Z80_8)
                        .Case("BFD_RELOC_16", ELF::R_Z80_16)
                        .Case("BFD_RELOC_24", ELF::R_Z80_24)
                        .Case("BFD_RELOC_32", ELF::R_Z80_32)
                        .Default(-1u);
    if (Type != -1u)
      return MCFixupKind(FirstLiteralRelocationKind + Type);
    return std::nullopt;
  }

  bool finishLayout() const override {
    /* Resolve extraction aliases after all assignments have been parsed.
       The operand width stays fixed, so selecting a relocation here leaves
       instruction offsets and relative branches unchanged. */
    for (MCSection &Section : *Asm)
      for (MCFragment &Fragment : Section) {
        SmallVector<MCFixup, 4> ExtraFixups;
        for (MCFixup &Fixup : Fragment.getFixups()) {
          unsigned Size = Fixup.getKind() == FK_Data_1       ? 1
                          : Fixup.getKind() == FK_Data_2     ? 2
                          : Fixup.getKind() == Z80::fixup_24 ? 3
                          : Fixup.getKind() == FK_Data_4     ? 4
                                                             : 0;
          if (!Size || Fixup.isPCRel())
            continue;
          const MCExpr *Expr = Fixup.getValue();
          auto Byte = Z80::getExtractOffset(Expr, Size, *Asm);
          if (!Byte)
            continue;
          unsigned Offset = Fixup.getOffset();
          if (Size == 2 && (*Byte == 0 || *Byte == 2)) {
            Fixup = MCFixup::create(Offset, Expr,
                                    MCFixupKind(Z80::fixup_word0 + *Byte / 2));
          } else {
            // Keep the entire addend on every relocation so carries occur
            // before extraction, including words starting at an odd byte.
            Fixup = MCFixup::create(Offset, Expr,
                                    MCFixupKind(Z80::fixup_byte0 + *Byte));
            for (unsigned I = 1; I < Size; ++I)
              ExtraFixups.push_back(MCFixup::create(
                  Offset + I, Expr, MCFixupKind(Z80::fixup_byte0 + *Byte + I)));
          }
        }
        Fragment.appendFixups(ExtraFixups);
      }
    Z80::inlineExtractAliases(*Asm);
    return false;
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    switch (Kind) {
    case Z80::fixup_24:
      return {"fixup_24", 0, 24, 0};
    case Z80::fixup_8_pcrel:
      return {"fixup_8_pcrel", 0, 8, 0};
    case Z80::fixup_8_disp:
      return {"fixup_8_disp", 0, 8, 0};
    case Z80::fixup_byte0:
    case Z80::fixup_byte1:
    case Z80::fixup_byte2:
    case Z80::fixup_byte3:
      return {"fixup_byte", 0, 8, 0};
    case Z80::fixup_word0:
    case Z80::fixup_word1:
      return {"fixup_word", 0, 16, 0};
    default:
      return MCAsmBackend::getFixupKindInfo(Kind);
    }
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
    if (mc::isRelocation(Fixup.getKind()))
      return;
    unsigned Bits = getFixupKindInfo(Fixup.getKind()).TargetSize;
    if (Fixup.getKind() >= Z80::fixup_byte0 &&
        Fixup.getKind() <= Z80::fixup_word1) {
      unsigned Shift = Fixup.getKind() <= Z80::fixup_byte3
                           ? 8 * (Fixup.getKind() - Z80::fixup_byte0)
                           : 16 * (Fixup.getKind() - Z80::fixup_word0);
      Value = (Value >> Shift) & ((uint64_t(1) << Bits) - 1);
    }
    if (IsResolved &&
        (Fixup.getKind() == Z80::fixup_8_pcrel ||
         Fixup.getKind() == Z80::fixup_8_disp) &&
        !isInt<8>(Value))
      getContext().reportError(Fixup.getLoc(), "Z80 displacement out of range");
    else if (IsResolved && !isIntN(Bits, Value) && !isUIntN(Bits, Value))
      getContext().reportError(Fixup.getLoc(), "Z80 fixup value out of range");
    for (unsigned I = 0; I != divideCeil(Bits, 8); ++I)
      Data[I] = Value >> (I * 8);
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *) const override {
    OS.write_zeros(Count);
    return true;
  }
};
} // namespace

MCAsmBackend *llvm::createZ80AsmBackend(const Target &T,
                                        const MCSubtargetInfo &STI,
                                        const MCRegisterInfo &MRI,
                                        const MCTargetOptions &Options) {
  return new Z80AsmBackend(
      STI.getTargetTriple().getOS() == Triple::UnknownOS
          ? ELF::ELFOSABI_NONE
          : MCELFObjectTargetWriter::getOSABI(STI.getTargetTriple().getOS()));
}
