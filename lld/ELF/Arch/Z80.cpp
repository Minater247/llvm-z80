//===- Z80.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class Z80 final : public TargetInfo {
public:
  Z80(Ctx &ctx) : TargetInfo(ctx) {
    defaultCommonPageSize = 1;
    defaultMaxPageSize = 1;
    defaultImageBase = 0;
    trapInstr = {0x76, 0x76, 0x76, 0x76}; // HALT
  }
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

uint32_t Z80::calcEFlags() const {
  uint32_t flags = 0;
  for (InputFile *file : ctx.objectFiles) {
    uint32_t f = cast<ObjFile<ELF32LE>>(file)->getObj().getHeader().e_flags;
    switch (f) {
    case EF_Z80_MACH_Z80:
    case EF_Z80_MACH_Z180:
    case EF_Z80_MACH_EZ80_Z80:
    case EF_Z80_MACH_EZ80_ADL:
      break;
    default:
      ErrAlways(ctx) << file << ": unsupported Z80 ELF flags: " << f;
      continue;
    }
    if (flags &&
        (flags == EF_Z80_MACH_EZ80_ADL) != (f == EF_Z80_MACH_EZ80_ADL)) {
      ErrAlways(ctx) << file << ": cannot link Z80 16-bit and eZ80 ADL objects";
      continue;
    }
    // Retain the most capable required ISA within the selected address mode.
    flags = std::max(flags, f);
  }
  return flags ? flags : EF_Z80_MACH_Z80;
}

RelExpr Z80::getRelExpr(RelType type, const Symbol &s,
                        const uint8_t *loc) const {
  switch (type) {
  case R_Z80_NONE:
    return R_NONE;
  case R_Z80_8_PCREL:
    return R_PC;
  case R_Z80_8:
  case R_Z80_8_DIS:
  case R_Z80_16:
  case R_Z80_24:
  case R_Z80_32:
  case R_Z80_BYTE0:
  case R_Z80_BYTE1:
  case R_Z80_BYTE2:
  case R_Z80_BYTE3:
  case R_Z80_WORD0:
  case R_Z80_WORD1:
  case R_Z80_16_BE:
    return R_ABS;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type.v
             << ") against symbol " << &s;
    return R_NONE;
  }
}

int64_t Z80::getImplicitAddend(const uint8_t *buf, RelType type) const {
  if (type != R_Z80_NONE)
    Err(ctx) << getErrorLoc(ctx, buf)
             << "Z80 requires RELA relocations with explicit addends";
  return 0;
}

void Z80::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_Z80_NONE:
    break;
  case R_Z80_8:
    checkIntUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_Z80_8_DIS:
  case R_Z80_8_PCREL:
    checkInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_Z80_16:
    checkIntUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_Z80_24:
    checkIntUInt(ctx, loc, val, 24, rel);
    write16le(loc, val);
    loc[2] = val >> 16;
    break;
  case R_Z80_32:
    write32le(loc, val);
    break;
  case R_Z80_BYTE0:
  case R_Z80_BYTE1:
  case R_Z80_BYTE2:
  case R_Z80_BYTE3:
    *loc = val >> (8 * (rel.type - R_Z80_BYTE0));
    break;
  case R_Z80_WORD0:
  case R_Z80_WORD1:
    write16le(loc, val >> (16 * (rel.type - R_Z80_WORD0)));
    break;
  case R_Z80_16_BE:
    checkIntUInt(ctx, loc, val, 16, rel);
    write16be(loc, val);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

void elf::setZ80TargetInfo(Ctx &ctx) { ctx.target.reset(new Z80(ctx)); }
