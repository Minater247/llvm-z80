//===-- Z80MCAsmInfo.cpp - Z80 asm properties -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the Z80MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "Z80MCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

void Z80MCAsmInfoELF::anchor() { }

Z80MCAsmInfoELF::Z80MCAsmInfoELF(const Triple &T) {
  bool Is16Bit = T.isArch16Bit() || T.getEnvironment() == Triple::CODE16;
  CodePointerSize = CalleeSaveStackSlotSize = Is16Bit ? 2 : 3;
  MaxInstLength = 6;
  DollarIsPC = true;
  SeparatorString = nullptr;
  CommentString = ";";
  AssemblerDialect = !Is16Bit;
  SupportsQuotedNames = false;

  ZeroDirective = "\tds\t";
  AsciiDirective = nullptr;
  AscizDirective = nullptr;
  CharacterLiteralSyntax = ACLS_Unknown;

  Data8bitsDirective = "\tdb\t";
  Data16bitsDirective = "\tdw\t";
  Data32bitsDirective = "\td32\t";
  Data64bitsDirective = nullptr;

  GlobalDirective = "\t.global\t";
  WeakDirective = "\t.weak\t";

  UseIntegratedAssembler = false;
  UseLogicalShr = false;

  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI; // TODO: This crashes on DwarfCFI - determine why and fix
}

bool Z80MCAsmInfoELF::isAcceptableChar(char C) const {
  return MCAsmInfo::isAcceptableChar(C);// || C == '%' || C == '^';
}

bool Z80MCAsmInfoELF::shouldOmitSectionDirective(StringRef SectionName) const {
  return false;
}
