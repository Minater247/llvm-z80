// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80FIXUPKINDS_H
#define LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace Z80 {
enum Fixups {
  fixup_24 = FirstTargetFixupKind,
  fixup_8_pcrel,
  fixup_8_disp,
};

} // namespace Z80
} // namespace llvm
#endif
