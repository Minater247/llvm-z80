// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Z80FixupKinds.h"
#include "Z80MCTargetDesc.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"

using namespace llvm;

namespace {
class Z80ELFObjectWriter final : public MCELFObjectTargetWriter {
public:
  explicit Z80ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(false, OSABI, ELF::EM_Z80, true) {}

  unsigned getRelocType(const MCFixup &Fixup, const MCValue &,
                        bool IsPCRel) const override {
    if (IsPCRel) {
      if (Fixup.getKind() == Z80::fixup_8_pcrel)
        return ELF::R_Z80_8_PCREL;
      getContext().reportError(Fixup.getLoc(),
                               "unsupported Z80 PC-relative relocation");
      return ELF::R_Z80_NONE;
    }
    switch (Fixup.getKind()) {
    case FK_Data_1:
      return ELF::R_Z80_8;
    case FK_Data_2:
      return ELF::R_Z80_16;
    case FK_Data_4:
      return ELF::R_Z80_32;
    case Z80::fixup_24:
      return ELF::R_Z80_24;
    case Z80::fixup_8_disp:
      return ELF::R_Z80_8_DIS;
    default:
      getContext().reportError(Fixup.getLoc(), "unsupported Z80 relocation");
      return ELF::R_Z80_NONE;
    }
  }
};
} // namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createZ80ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<Z80ELFObjectWriter>(OSABI);
}
