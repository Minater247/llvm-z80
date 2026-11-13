//===- Z80InstPrinter.cpp - Convert Z80 MCInst to assembly ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file includes code common for rendering MCInst instances as (e)Z80
// assembly.
//
//===----------------------------------------------------------------------===//

#include "Z80InstPrinterCommon.h"
#include "Z80InstPrinter.h"
#include "Z80BaseInfo.h"
#include "Z80MCTargetDesc.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include <cstdlib>
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

static cl::opt<bool> PrintZeroOffset(
    "z80-print-zero-offset",
    cl::desc("Print ix + 0 instead of ix"),
    cl::init(false),
    cl::Hidden);
static cl::opt<bool> AddNegativeOffset(
    "z80-add-negative-offset",
    cl::desc("Print ix + -1 instead of ix - 1"),
    cl::init(false),
    cl::Hidden);

Z80InstPrinterCommon::Z80InstPrinterCommon(const MCAsmInfo &MAI,
                                           const MCInstrInfo &MII,
                                           const MCRegisterInfo &MRI)
    : MCInstPrinter(MAI, MII, MRI), PrintZeroOffset(::PrintZeroOffset),
      AddNegativeOffset(::AddNegativeOffset) {}

bool Z80InstPrinterCommon::applyTargetSpecificCLOption(StringRef Opt) {
  if (Opt == "print-zero-offset") {
    PrintZeroOffset = true;
    return true;
  }
  if (Opt == "no-print-zero-offset") {
    PrintZeroOffset = false;
    return true;
  }
  if (Opt == "add-negative-offset") {
    AddNegativeOffset = true;
    return true;
  }
  if (Opt == "subtract-positive-offset") {
    AddNegativeOffset = false;
    return true;
  }
  return false;
}

void Z80InstPrinterCommon::printRegName(raw_ostream &OS, MCRegister Reg) {
  markup(OS, Markup::Register) << getRegName(Reg.id());
}

void Z80InstPrinterCommon::printInst(const MCInst *MI, uint64_t Address,
                                     StringRef Annot,
                                     const MCSubtargetInfo &STI,
                                     raw_ostream &OS) {
  unsigned Prefix = MI->getFlags() & Z80II::ExplicitPrefixMask;
  StringRef Suffix;
  if (Prefix)
    Suffix = Prefix == 0x40 ? "sis" : Prefix == 0x49 ? "lis"
                                    : Prefix == 0x52 ? "sil" : "lil";
  else {
    unsigned Mode = MII.get(MI->getOpcode()).TSFlags & Z80II::ModeMask;
    if (Mode == Z80II::Z80Mode && STI.hasFeature(Z80::Mode24Bit))
      Suffix = "sis";
    else if (Mode == Z80II::EZ80Mode && !STI.hasFeature(Z80::Mode24Bit))
      Suffix = "lil";
  }
  StringRef PrintedSuffix = StringRef(getMnemonic(*MI).first)
                                .split('\t').first.split('.').second;
  if (Suffix == PrintedSuffix) {
    printInstruction(MI, Address, OS);
  } else {
    SmallString<128> Text;
    raw_svector_ostream Buffer(Text);
    printInstruction(MI, Address, Buffer);
    StringRef Body = StringRef(Text).ltrim();
    auto [Mnemonic, Operands] = Body.split('\t');
    OS << '\t' << Mnemonic.split('.').first;
    if (!Suffix.empty()) OS << '.' << Suffix;
    if (!Operands.empty()) OS << '\t' << Operands;
  }
  printAnnotation(OS, Annot);
}

void Z80InstPrinterCommon::printOperand(const MCInst *MI, unsigned OpNo,
                                        raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(OS, Op.getReg());
    return;
  }

  if (Op.isImm()) {
    markup(OS, Markup::Immediate) << formatImm(Op.getImm());
    return;
  }

  {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    [[maybe_unused]] MCInstPrinter::WithMarkup ImmediateMarkup =
        markup(OS, Markup::Immediate);
    MAI.printExpr(OS, *Op.getExpr());
  }
}

void Z80InstPrinterCommon::printOperand(const MCInst *MI, uint64_t Address,
                                        unsigned Op, raw_ostream &OS) {
  return printOperand(MI, Op, OS);
}

void Z80InstPrinterCommon::printPCRel(const MCInst *MI, uint64_t Address,
                                      unsigned OpNo,
                                      raw_ostream &OS) {
  if (!MI->getOperand(OpNo).isImm()) {
    printOperand(MI, OpNo, OS);
    return;
  }
  int64_t Offset = MI->getOperand(OpNo).getImm() + 2 +
                   bool(MI->getFlags() & Z80II::ExplicitPrefixMask);
  OS << '$';
  if (Offset >= 0) OS << '+';
  OS << Offset;
}

void Z80InstPrinterCommon::printCondCode(const MCInst *MI, unsigned Op,
                                         raw_ostream &OS) {
  MCInstPrinter::WithMarkup CondMarkup = markup(OS, Markup::Immediate);
  switch (unsigned CondCode = MI->getOperand(Op).getImm()) {
  default:
    llvm_unreachable("Invalid condition code operand!");
  case 0:
    CondMarkup << 'n';
    LLVM_FALLTHROUGH;
  case 1:
    CondMarkup << 'z';
    break;
  case 2:
    CondMarkup << 'n';
    LLVM_FALLTHROUGH;
  case 3:
    CondMarkup << 'c';
    break;
  case 4:
  case 5:
  case 6:
    CondMarkup << 'p';
    switch (CondCode) {
    case 4:
      CondMarkup << 'o';
      break;
    case 5:
      CondMarkup << 'e';
      break;
    }
    break;
  case 7:
    CondMarkup << 'm';
    break;
  }
}

void Z80InstPrinterCommon::printOffset(const MCInst *MI, unsigned Op,
                                       raw_ostream &OS) {
  printOperand(MI, Op, OS);
  auto Offset = MI->getOperand(Op + 1).getImm();
  assert(isInt<8>(Offset) && "Offset out of range!");
  if (Offset || PrintZeroOffset)
    OS << ' ' << (Offset >= 0 || AddNegativeOffset ? '+' : '-') << ' '
       << formatImm(AddNegativeOffset ? Offset : std::abs(Offset));
}

void Z80InstPrinterCommon::printIndirect(const MCInst *MI, unsigned Op,
                                         raw_ostream &OS) {
  [[maybe_unused]] MCInstPrinter::WithMarkup MemoryMarkup =
      markup(OS, Markup::Memory);
  OS << '(';
  printOperand(MI, Op, OS);
  OS << ')';
}

void Z80InstPrinterCommon::printIndirectOffset(const MCInst *MI, unsigned Op,
                                               raw_ostream &OS) {
  [[maybe_unused]] MCInstPrinter::WithMarkup MemoryMarkup =
      markup(OS, Markup::Memory);
  OS << '(';
  printOffset(MI, Op, OS);
  OS << ')';
}
