//===- Z80TargetStreamer.cpp - Z80TargetStreamer class --*- C++ -*---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Z80TargetStreamer class.
//
//===----------------------------------------------------------------------===//

#include "Z80TargetStreamer.h"
#include "Z80MCTargetDesc.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/FormattedStream.h"

using namespace llvm;

Z80TargetStreamer::Z80TargetStreamer(MCStreamer &S)
    : MCTargetStreamer(S) {}

Z80TargetAsmStreamer::Z80TargetAsmStreamer(MCStreamer &S,
                                           formatted_raw_ostream &OS)
    : Z80TargetStreamer(S), MAI(S.getContext().getAsmInfo()), OS(OS) {}

void Z80TargetAsmStreamer::emitLabel(MCSymbol *Symbol) {
  if (Symbol->isTemporary())
    emitLocal(Symbol);
}

void Z80TargetAsmStreamer::emitAlign(Align Alignment) {
  unsigned Val = Alignment.value();
  if (Val > 1) {
    OS << "\t.balign\t" << Val << '\n';
  }
}

void Z80TargetAsmStreamer::emitBlock(uint64_t NumBytes) {
  if (NumBytes)
    OS << "\tds\t" << NumBytes << '\n';
}

void Z80TargetAsmStreamer::emitLocal(MCSymbol *Symbol) {
  OS << "\t.local\t";
  Symbol->print(OS, MAI);
  OS << '\n';
}

void Z80TargetAsmStreamer::emitWeakGlobal(MCSymbol *Symbol) {
  OS << "\t.weak\t";
  Symbol->print(OS, MAI);
  OS << '\n';
}

void Z80TargetAsmStreamer::emitGlobal(MCSymbol *Symbol) {
  OS << "\t.global\t";
  Symbol->print(OS, MAI);
  OS << '\n';
}

void Z80TargetAsmStreamer::emitExtern(MCSymbol *Symbol) {
  OS << "\t.extern\t";
  Symbol->print(OS, MAI);
  OS << '\n';
}

void Z80TargetAsmStreamer::emitCode16() {
  OS << "\t.code16\n";
}

void Z80TargetAsmStreamer::emitCode24() {
  OS << "\t.code24\n";
}

Z80TargetELFStreamer::Z80TargetELFStreamer(MCStreamer &S,
                                         const MCSubtargetInfo &STI)
    : Z80TargetStreamer(S) {
  static_cast<MCELFStreamer &>(S).getWriter().setELFHeaderEFlags(
      Z80_MC::getELFFlags(STI));
}

void Z80TargetELFStreamer::emitAlign(Align Alignment) {
  getStreamer().emitValueToAlignment(Alignment);
}
void Z80TargetELFStreamer::emitBlock(uint64_t NumBytes) {
  getStreamer().emitZeros(NumBytes);
}
void Z80TargetELFStreamer::emitLocal(MCSymbol *Symbol) {
  getStreamer().emitSymbolAttribute(Symbol, MCSA_Local);
}
void Z80TargetELFStreamer::emitWeakGlobal(MCSymbol *Symbol) {
  getStreamer().emitSymbolAttribute(Symbol, MCSA_Weak);
}
void Z80TargetELFStreamer::emitGlobal(MCSymbol *Symbol) {
  getStreamer().emitSymbolAttribute(Symbol, MCSA_Global);
}
