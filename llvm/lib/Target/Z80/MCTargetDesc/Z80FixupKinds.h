// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80FIXUPKINDS_H
#define LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"
#include <optional>

namespace llvm {
class MCAssembler;
namespace Z80 {
enum Fixups {
  fixup_24 = FirstTargetFixupKind,
  fixup_8_pcrel,
  fixup_8_disp,
  fixup_byte0,
  fixup_byte1,
  fixup_byte2,
  fixup_byte3,
  fixup_word0,
  fixup_word1,
};

/// Recognize a shifted or masked value selecting bytes of a 32-bit address.
/// On success, replace Expr with the value before extraction and return the
/// starting byte. Leave Expr unchanged for absolute values and
/// expressions that cannot be represented by Z80 extraction relocations.
std::optional<unsigned> getExtractOffset(const MCExpr *&Expr, unsigned Size,
                                         const MCAssembler &Asm);

/// Keep local extraction aliases in assembly expressions without exporting an
/// ELF symbol whose address would require an unsupported symbol-table
/// operation. Diagnose global aliases; resolved absolute assignments retain
/// their symbols.
void inlineExtractAliases(MCAssembler &Asm);
} // namespace Z80
} // namespace llvm
#endif
