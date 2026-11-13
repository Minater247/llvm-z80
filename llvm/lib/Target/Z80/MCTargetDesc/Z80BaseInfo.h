// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80BASEINFO_H
#define LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80BASEINFO_H

namespace llvm {
namespace Z80II {
// MCInst flags retain an explicitly written eZ80 mode prefix.
enum { ExplicitPrefixMask = 0xff };

enum {
  ModeShift = 0,
  AnyMode = 0 << ModeShift,
  CurMode = 1 << ModeShift,
  Z80Mode = 2 << ModeShift,
  EZ80Mode = 3 << ModeShift,
  ModeMask = 3 << ModeShift,

  PrefixShift = 2,
  Idx0Pre = 1 << 0 << PrefixShift,
  Idx1Pre = 1 << 1 << PrefixShift,
  CBPre = 1 << 2 << PrefixShift,
  EDPre = 1 << 3 << PrefixShift,
  PrefixMask = ((1 << 4) - 1) << PrefixShift,

  HasImm = 1 << 6,
  HasOff = 1 << 7,

  OpcodeShift = 8,
  OpcodeMask = 0xFF << OpcodeShift
};
} // namespace Z80II
} // namespace llvm

#endif
