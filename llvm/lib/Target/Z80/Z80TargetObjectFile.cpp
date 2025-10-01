//===-- Z80TargetObjectFile.cpp - Z80 Object Info -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80TargetObjectFile.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/BinaryFormat/ELF.h"
using namespace llvm;

void Z80ELFTargetObjectFile::anchor() {}

MCSection *Z80ELFTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // Disable mergeable sections for now.
  if (Kind.isMergeableCString() || Kind.isMergeableConst())
    Kind = SectionKind::getReadOnly();
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}

MCSection *Z80ELFTargetObjectFile::getSectionForJumpTable(
    const Function &F, const TargetMachine &TM) const {
  // Always place jump tables into a dedicated read-only section so they do not
  // share address space with writable data or text that we may patch/relax.
  // Use a single section name across the TU; uniqueness is not required.
  // Keep it read-only and allocatable (.rodata-like).
  MCContext &Ctx = getContext();
  MCSectionELF *Sec = Ctx.getELFSection(".rodata.jumptable", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  return static_cast<MCSection *>(Sec);
}
