// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Z80FixupKinds.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

namespace {
class Z80InlineAliasExpr final : public MCTargetExpr {
  const MCExpr *Value;

public:
  explicit Z80InlineAliasExpr(const MCExpr *Value) : Value(Value) {}
  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override {
    MAI->printExpr(OS, *Value);
  }
  bool evaluateAsRelocatableImpl(MCValue &Result,
                                 const MCAssembler *Asm) const override {
    return Value->evaluateAsRelocatable(Result, Asm);
  }
  bool inlineAssignedExpr() const override { return true; }
  void visitUsedExpr(MCStreamer &Streamer) const override {
    Streamer.visitUsedExpr(*Value);
  }
  MCFragment *findAssociatedFragment() const override {
    return Value->findAssociatedFragment();
  }
};
} // namespace

static const MCExpr *expandAlias(const MCExpr *Expr) {
  SmallPtrSet<const MCSymbol *, 8> Seen;
  while (const auto *Ref = dyn_cast<MCSymbolRefExpr>(Expr)) {
    const auto &Symbol = Ref->getSymbol();
    if (Ref->getSpecifier() || !Symbol.isVariable() ||
        !Seen.insert(&Symbol).second)
      break;
    Expr = Symbol.getVariableValue();
  }
  return Expr;
}

std::optional<unsigned> llvm::Z80::getExtractOffset(const MCExpr *&Expr,
                                                    unsigned Size,
                                                    const MCAssembler &Asm) {
  int64_t Constant;
  if ((Size < 1 || Size > 4) || Expr->evaluateAsAbsolute(Constant, Asm))
    return std::nullopt;

  const MCExpr *Value = expandAlias(Expr);
  bool Masked = false;
  if (auto *Binary = dyn_cast<MCBinaryExpr>(Value)) {
    if (Binary->getOpcode() == MCBinaryExpr::And) {
      int64_t Mask = (int64_t(1) << (8 * Size)) - 1;
      if (Binary->getRHS()->evaluateAsAbsolute(Constant, Asm) &&
          Constant == Mask)
        Value = Binary->getLHS();
      else if (Binary->getLHS()->evaluateAsAbsolute(Constant, Asm) &&
               Constant == Mask)
        Value = Binary->getRHS();
      else
        return std::nullopt;
      Masked = true;
    }
  }

  Value = expandAlias(Value);

  int64_t Shift = 0;
  if (auto *Binary = dyn_cast<MCBinaryExpr>(Value);
      Binary && (Binary->getOpcode() == MCBinaryExpr::AShr ||
                 Binary->getOpcode() == MCBinaryExpr::LShr)) {
    if (!Binary->getRHS()->evaluateAsAbsolute(Shift, Asm))
      return std::nullopt;
    Value = Binary->getLHS();
  } else if (!Masked) {
    return std::nullopt;
  }

  if (Shift < 0 || Shift > 32 - 8 * Size || Shift % 8 != 0)
    return std::nullopt;
  Expr = Value;
  return Shift / 8;
}

void llvm::Z80::inlineExtractAliases(MCAssembler &Asm) {
  SmallVector<const MCSymbol *, 8> Aliases;
  for (const MCSymbol &Symbol : Asm.symbols()) {
    if (!Symbol.isVariable())
      continue;
    const MCExpr *Value = Symbol.getVariableValue();
    if (getExtractOffset(Value, 1, Asm) || getExtractOffset(Value, 2, Asm) ||
        getExtractOffset(Value, 3, Asm) || getExtractOffset(Value, 4, Asm))
      Aliases.push_back(&Symbol);
  }
  /* Collect aliases before marking them: a forward chain must see the
     original expression of each intermediate assignment. ELF can relocate
     extracted bytes in section contents, but cannot encode extraction in a
     symbol's st_value. Local aliases therefore remain assembly-only names. */
  for (const MCSymbol *Symbol : Aliases) {
    const MCExpr *Value = Symbol->getVariableValue();
    const auto *ELFSymbol = cast<MCSymbolELF>(Symbol);
    if (ELFSymbol->isBindingSet() && ELFSymbol->getBinding() != ELF::STB_LOCAL)
      Asm.getContext().reportError(Value->getLoc(),
                                   "cannot export Z80 extraction alias");
    const_cast<MCSymbol *>(Symbol)->setVariableValue(
        new (Asm.getContext()) Z80InlineAliasExpr(Value));
  }
}
