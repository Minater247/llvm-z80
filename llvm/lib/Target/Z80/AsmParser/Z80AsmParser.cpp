//===- Z80AsmParser.cpp - Z80 assembly parser -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80BaseInfo.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "MCTargetDesc/Z80TargetStreamer.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

namespace {

static MCRegister shortRegister(MCRegister R) {
  switch (R.id()) {
  case Z80::UBC:
    return Z80::BC;
  case Z80::UDE:
    return Z80::DE;
  case Z80::UHL:
    return Z80::HL;
  case Z80::UIX:
    return Z80::IX;
  case Z80::UIY:
    return Z80::IY;
  case Z80::SPL:
    return Z80::SPS;
  case Z80::UI:
    return Z80::I;
  default:
    return R;
  }
}

static MCRegister longRegister(MCRegister R) {
  switch (R.id()) {
  case Z80::BC:
    return Z80::UBC;
  case Z80::DE:
    return Z80::UDE;
  case Z80::HL:
    return Z80::UHL;
  case Z80::IX:
    return Z80::UIX;
  case Z80::IY:
    return Z80::UIY;
  case Z80::SPS:
    return Z80::SPL;
  case Z80::I:
    return Z80::UI;
  default:
    return R;
  }
}

class Z80Operand : public MCParsedAsmOperand {
public:
  enum Kind { Token, Register, Immediate, Memory, Offset, Condition } K;
  std::string Text;
  MCRegister Reg;
  const MCExpr *Expr = nullptr;
  SMLoc Start, End;

  Z80Operand(Kind K, SMLoc Start, SMLoc End) : K(K), Start(Start), End(End) {}
  bool isToken() const override { return K == Token; }
  bool isReg() const override { return K == Register; }
  bool isImm() const override { return K == Immediate; }
  bool isMem() const override { return K == Memory && !Reg; }
  bool isCC() const { return K == Condition; }
  bool isConstant() const {
    int64_t Value;
    return isImm() && Expr->evaluateAsAbsolute(Value);
  }
  bool isRPtr() const {
    MCRegister R = shortRegister(Reg);
    return K == Memory && !Expr &&
           (R == Z80::BC || R == Z80::DE || R == Z80::HL || R == Z80::IX ||
            R == Z80::IY);
  }
  bool isAPtr() const {
    MCRegister R = shortRegister(Reg);
    return isRPtr() && (R == Z80::HL || R == Z80::IX || R == Z80::IY);
  }
  bool isHPtr() const {
    return isRPtr() && shortRegister(Reg) == Z80::HL;
  }
  bool isOff() const {
    MCRegister R = shortRegister(Reg);
    return K == Memory && (R == Z80::IX || R == Z80::IY);
  }
  bool isOffset() const {
    MCRegister R = shortRegister(Reg);
    return K == Offset && (R == Z80::IX || R == Z80::IY);
  }
  MCRegister getReg() const override { return Reg; }
  StringRef getToken() const { return Text; }
  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }
  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    if (isToken())
      OS << Text;
    else if (Reg)
      OS << "register " << Reg.id();
    if (Expr)
      MAI.printExpr(OS, *Expr);
  }
  static std::unique_ptr<Z80Operand> token(StringRef Text, SMLoc Loc) {
    auto Op = std::make_unique<Z80Operand>(Token, Loc, Loc);
    Op->Text = Text.str();
    return Op;
  }
  static void addExpr(MCInst &Inst, const MCExpr *Expr) {
    int64_t Value = 0;
    if (!Expr || Expr->evaluateAsAbsolute(Value))
      Inst.addOperand(MCOperand::createImm(Value));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1);
    Inst.addOperand(MCOperand::createReg(Reg));
  }
  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1);
    addExpr(Inst, Expr);
  }
  void addBranchOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1);
    // Assembly operands name branch destinations, including numeric addresses.
    Inst.addOperand(MCOperand::createExpr(Expr));
  }
  void addMemOperands(MCInst &Inst, unsigned N) const {
    addImmOperands(Inst, N);
  }
  void addPtrOperands(MCInst &Inst, unsigned N) const {
    addRegOperands(Inst, N);
  }
  void addOffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2);
    Inst.addOperand(MCOperand::createReg(Reg));
    addExpr(Inst, Expr);
  }
};

class Z80AsmParser : public MCTargetAsmParser {
  bool Operand24 = false;
  unsigned Prefix = 0;
  std::string Mnemonic;

  MCRegister registerByName(StringRef Name) const {
    return StringSwitch<MCRegister>(Name.lower())
        .Case("a", Z80::A)
        .Case("b", Z80::B)
        .Case("c", Z80::C)
        .Case("d", Z80::D)
        .Case("e", Z80::E)
        .Case("h", Z80::H)
        .Case("l", Z80::L)
        .Case("f", Z80::F)
        .Case("af", Z80::AF)
        .Case("bc", Z80::BC)
        .Case("de", Z80::DE)
        .Case("hl", Z80::HL)
        .Case("ix", Z80::IX)
        .Case("iy", Z80::IY)
        .Case("sp", Z80::SPS)
        .Case("ixh", Z80::IXH)
        .Case("ixl", Z80::IXL)
        .Case("iyh", Z80::IYH)
        .Case("iyl", Z80::IYL)
        .Case("i", Z80::I)
        .Case("r", Z80::R)
        .Case("mb", Z80::MB)
        .Default(MCRegister());
  }
  bool parseOperand(OperandVector &Operands);
  bool parseData(unsigned Size);
  bool parseSpace();
  bool parseInstruction(ParseInstructionInfo &, StringRef Name, SMLoc Loc,
                        OperandVector &Operands) override;
  ParseStatus parseDirective(AsmToken Directive) override;
  bool parseRegister(MCRegister &Reg, SMLoc &Start, SMLoc &End) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &Start,
                               SMLoc &End) override;
  bool matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo, bool InlineAsm) override;
  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;
#define GET_ASSEMBLER_HEADER
#include "Z80GenAsmMatcher.inc"
public:
  Z80AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
               const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};
} // namespace

ParseStatus Z80AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &Start,
                                           SMLoc &End) {
  const AsmToken &Tok = getParser().getTok();
  if (!Tok.is(AsmToken::Identifier) || !(Reg = registerByName(Tok.getString())))
    return ParseStatus::NoMatch;
  Start = Tok.getLoc();
  End = Tok.getEndLoc();
  if (STI->hasFeature(Z80::Mode24Bit))
    Reg = longRegister(Reg);
  getParser().Lex();
  return ParseStatus::Success;
}

bool Z80AsmParser::parseRegister(MCRegister &Reg, SMLoc &Start, SMLoc &End) {
  return !tryParseRegister(Reg, Start, End).isSuccess() &&
         Error(getParser().getTok().getLoc(), "expected Z80 register");
}

bool Z80AsmParser::parseOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc Start = Parser.getTok().getLoc();
  bool Indirect = parseOptionalToken(AsmToken::LParen);
  MCRegister Reg;
  if (Parser.getTok().is(AsmToken::Identifier)) {
    StringRef Name = Parser.getTok().getString();
    if (!Indirect && Name.equals_insensitive("af'")) {
      Operands.push_back(Z80Operand::token("af'", Start));
      Parser.Lex();
      return false;
    }
    // A conditional branch's first operand is a condition only when followed
    // by a comma. Otherwise names such as 'c' remain valid branch symbols.
    bool Branch = Mnemonic == "jp" || Mnemonic == "jr" || Mnemonic == "call" ||
                  Mnemonic == "djnz" || Mnemonic == "ret";
    if (!Indirect && Branch) {
      int CC = StringSwitch<int>(Name.lower())
                   .Case("nz", 0)
                   .Case("z", 1)
                   .Case("nc", 2)
                   .Case("c", 3)
                   .Case("po", 4)
                   .Case("pe", 5)
                   .Case("p", 6)
                   .Case("m", 7)
                   .Default(-1);
      if (Operands.size() == 1 && CC >= 0 && Mnemonic != "djnz" &&
          (Mnemonic == "ret" ||
           Parser.getLexer().peekTok().is(AsmToken::Comma))) {
        auto Op = std::make_unique<Z80Operand>(Z80Operand::Condition, Start,
                                               Parser.getTok().getEndLoc());
        Op->Expr = MCConstantExpr::create(CC, getContext());
        Operands.push_back(std::move(Op));
        Parser.Lex();
        return false;
      }
    } else {
      Reg = registerByName(Name);
    }
  }
  const MCExpr *Expr = nullptr;
  SMLoc End;
  bool ParsedParen = false;
  if (Indirect && !Reg) {
    // Parentheses enclosing the whole operand denote memory. A following
    // operator instead makes them part of an immediate, as in (symbol+1)>>8.
    // Look ahead using tokens so character literals and nested groups keep
    // the same lexical rules as the expression parser.
    SmallVector<AsmToken, 8> Tokens;
    unsigned Depth = 1;
    while (Depth && !Parser.getTok().is(AsmToken::EndOfStatement) &&
           !Parser.getTok().is(AsmToken::Eof)) {
      AsmToken Tok = Parser.getTok();
      Tokens.push_back(Tok);
      if (Tok.is(AsmToken::LParen))
        ++Depth;
      else if (Tok.is(AsmToken::RParen))
        --Depth;
      Parser.Lex();
    }
    Indirect = Parser.getTok().is(AsmToken::EndOfStatement) ||
               Parser.getTok().is(AsmToken::Comma);
    for (auto I = Tokens.rbegin(); I != Tokens.rend(); ++I)
      Parser.getLexer().UnLex(*I);
    if (Parser.parseParenExpression(Expr, End))
      return true;
    ParsedParen = true;
  } else if (Reg) {
    Parser.Lex();
    if (Parser.getTok().is(AsmToken::Plus) ||
        Parser.getTok().is(AsmToken::Minus)) {
      if (Parser.parseExpression(Expr))
        return true;
    }
  } else if (Parser.parseExpression(Expr)) {
    return true;
  }
  if (!ParsedParen)
    End = Parser.getTok().getLoc();
  if (Indirect && !ParsedParen && parseToken(AsmToken::RParen, "expected ')'"))
    return true;

  // These parentheses are literal pieces of the instruction syntax. Memory
  // operands elsewhere carry the base and displacement as one parsed operand.
  bool LiteralIndirect = Indirect && Reg && !Expr &&
                         (Mnemonic == "jp" || Mnemonic == "ex" ||
                          ((Mnemonic == "in" || Mnemonic == "out") &&
                           (Reg == Z80::C || Reg == Z80::BC)));
  if (LiteralIndirect)
    Operands.push_back(Z80Operand::token("(", Start));
  auto Kind = Indirect && !LiteralIndirect ? Z80Operand::Memory
              : Reg && (Expr || Mnemonic == "pea" ||
                        (Mnemonic == "lea" && Operands.size() == 2))
                  ? Z80Operand::Offset
              : Reg ? Z80Operand::Register
                    : Z80Operand::Immediate;
  auto Op = std::make_unique<Z80Operand>(Kind, Start, End);
  Op->Reg = Reg;
  if (Reg && Kind != Z80Operand::Register && Operand24)
    Op->Reg = longRegister(Reg);
  Op->Expr = Expr;
  Operands.push_back(std::move(Op));
  if (LiteralIndirect)
    Operands.push_back(Z80Operand::token(")", End));
  return false;
}

bool Z80AsmParser::parseData(unsigned Size) {
  do {
    if (Size == 1 && getParser().getTok().is(AsmToken::String)) {
      std::string Data;
      if (getParser().parseEscapedString(Data))
        return true;
      getStreamer().emitBytes(Data);
    } else {
      const MCExpr *Expr;
      SMLoc Loc = getParser().getTok().getLoc();
      if (getParser().parseExpression(Expr))
        return true;
      getStreamer().emitValue(Expr, Size, Loc);
    }
  } while (parseOptionalToken(AsmToken::Comma));
  return parseEOL();
}

bool Z80AsmParser::parseSpace() {
  const MCExpr *Count;
  SMLoc Loc = getParser().getTok().getLoc();
  int64_t AbsoluteCount, Fill = 0;
  if (getParser().parseExpression(Count))
    return true;
  if (Count->evaluateAsAbsolute(AbsoluteCount) && AbsoluteCount < 0)
    return Error(Loc, "negative space allocation");
  if (parseOptionalToken(AsmToken::Comma) &&
      getParser().parseAbsoluteExpression(Fill))
    return true;
  if (parseEOL())
    return true;
  getStreamer().emitFill(*Count, Fill, Loc);
  return false;
}

ParseStatus Z80AsmParser::parseDirective(AsmToken Directive) {
  std::string Name = Directive.getString().lower();
  unsigned Size = StringSwitch<unsigned>(Name)
                      .Cases({".db", ".defb"}, 1)
                      .Cases({".dw", ".defw"}, 2)
                      .Case(".d24", 3)
                      .Case(".d32", 4)
                      .Default(0);
  if (Size)
    return parseData(Size);
  if (Name == ".ds" || Name == ".defs")
    return parseSpace();
  if (Name != ".code16" && Name != ".code24")
    return ParseStatus::NoMatch;
  bool ADL = Name == ".code24";
  if (ADL && !STI->hasFeature(Z80::FeatureEZ80))
    return Error(Directive.getLoc(), ".code24 requires eZ80");
  if (parseEOL())
    return ParseStatus::Failure;
  auto &NewSTI = copySTI();
  FeatureBitset Features = NewSTI.getFeatureBits();
  Features.set(ADL ? Z80::Mode24Bit : Z80::Mode16Bit);
  Features.reset(ADL ? Z80::Mode16Bit : Z80::Mode24Bit);
  NewSTI.setFeatureBits(Features);
  auto &TS =
      *static_cast<Z80TargetStreamer *>(getStreamer().getTargetStreamer());
  if (ADL)
    TS.emitCode24();
  else
    TS.emitCode16();
  setAvailableFeatures(ComputeAvailableFeatures(Features));
  return ParseStatus::Success;
}

bool Z80AsmParser::parseInstruction(ParseInstructionInfo &, StringRef Name,
                                    SMLoc Loc, OperandVector &Operands) {
  Mnemonic = Name.lower();
  unsigned Size = StringSwitch<unsigned>(Mnemonic)
                      .Cases({"db", "defb"}, 1)
                      .Cases({"dw", "defw"}, 2)
                      .Case("d24", 3)
                      .Case("d32", 4)
                      .Default(0);
  if (Size)
    return parseData(Size);
  if (Mnemonic == "ds" || Mnemonic == "defs")
    return parseSpace();
  Operand24 = STI->hasFeature(Z80::Mode24Bit);
  Prefix = 0;
  StringRef Suffix = StringRef(Mnemonic).split('.').second;
  if (!Suffix.empty()) {
    if (!STI->hasFeature(Z80::FeatureEZ80))
      return Error(Loc, "instruction suffix requires eZ80");
    Prefix = StringSwitch<unsigned>(Suffix)
                 .Case("sis", 0x40)
                 .Case("lis", 0x49)
                 .Case("sil", 0x52)
                 .Case("lil", 0x5b)
                 .Case("s", Operand24 ? 0x52 : 0x40)
                 .Case("l", Operand24 ? 0x5b : 0x49)
                 .Case("is", Operand24 ? 0x49 : 0x40)
                 .Case("il", Operand24 ? 0x5b : 0x52)
                 .Default(0);
    if (!Prefix)
      return Error(Loc, "invalid eZ80 instruction suffix");
    Operand24 = Prefix == 0x49 || Prefix == 0x5b;
    Mnemonic.resize(Mnemonic.find('.'));
  }
  if (Mnemonic == "sll")
    Mnemonic = "sli";
  FeatureBitset Features = STI->getFeatureBits();
  Features.set(Operand24 ? Z80::Mode24Bit : Z80::Mode16Bit);
  Features.reset(Operand24 ? Z80::Mode16Bit : Z80::Mode24Bit);
  setAvailableFeatures(ComputeAvailableFeatures(Features));
  Operands.push_back(Z80Operand::token(Mnemonic, Loc));
  if (!getParser().getTok().is(AsmToken::EndOfStatement)) {
    do {
      if (parseOperand(Operands))
        return true;
    } while (parseOptionalToken(AsmToken::Comma));
  }
  if (parseEOL())
    return true;
  if (Operands.size() == 3 &&
      (Mnemonic == "and" || Mnemonic == "or" || Mnemonic == "xor" ||
       Mnemonic == "sub" || Mnemonic == "cp") &&
      Operands[1]->isReg() && Operands[1]->getReg() == Z80::A)
    Operands.erase(Operands.begin() + 1);
  return false;
}

bool Z80AsmParser::matchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                           OperandVector &Operands,
                                           MCStreamer &Out, uint64_t &ErrorInfo,
                                           bool InlineAsm) {
  if (Operands.empty())
    return false;
  MCInst Inst;
  FeatureBitset Missing;
  unsigned Result = MatchInstructionImpl(Operands, Inst, ErrorInfo, Missing,
                                         InlineAsm, Operand24 ? 1 : 0);
  if (Result == Match_Success) {
    if (!STI->hasFeature(Z80::FeatureIdxHalf))
      for (const MCOperand &Op : Inst)
        if (Op.isReg() && (Op.getReg() == Z80::IXH || Op.getReg() == Z80::IXL ||
                           Op.getReg() == Z80::IYH || Op.getReg() == Z80::IYL))
          return Error(Loc, "instruction requires index half registers");
    Inst.setLoc(Loc);
    Inst.setFlags(Prefix);
    Opcode = Inst.getOpcode();
    Out.emitInstruction(Inst, *STI);
    return false;
  }
  if (Result == Match_MissingFeature)
    return Error(Loc, "instruction requires an unavailable Z80 CPU feature");
  if (Result == Match_MnemonicFail)
    return Error(Loc, "invalid instruction mnemonic");
  if (ErrorInfo < Operands.size())
    Loc = Operands[ErrorInfo]->getStartLoc();
  return Error(Loc, "invalid operand for instruction");
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeZ80AsmParser() {
  RegisterMCAsmParser<Z80AsmParser> Z(getTheZ80Target());
  RegisterMCAsmParser<Z80AsmParser> E(getTheEZ80Target());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "Z80GenAsmMatcher.inc"

unsigned Z80AsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                  unsigned Kind) {
  auto &Op = static_cast<Z80Operand &>(AsmOp);
  if (!Op.isReg())
    return Match_InvalidOperand;
  MCRegister Original = Op.Reg;
  Op.Reg = longRegister(Original);
  if (Op.Reg == Original)
    Op.Reg = shortRegister(Original);
  if (validateOperandClass(Op, static_cast<MatchClassKind>(Kind), *STI) ==
      Match_Success)
    return Match_Success;
  Op.Reg = Original;
  return Match_InvalidOperand;
}
