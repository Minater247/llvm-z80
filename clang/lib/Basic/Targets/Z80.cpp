//===--- Z80.cpp - Implement Z80 target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Z80 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "clang/Basic/MacroBuilder.h"
#include "llvm/ADT/StringSwitch.h"

#include <cctype>
#include <optional>

namespace clang {
namespace targets {

static const char *const Z80GCCRegNames[] = {
    "a", "bc", "de", "hl", "ix", "iy", "sp",
};

static const char *const EZ80GCCRegNames[] = {
    "a", "bc", "de", "hl", "ix", "iy", "sps", "spl",
};

const TargetInfo::AddlRegName AddlRegNames[] = {
    {{"b", "c"}, 1},
    {{"d", "e"}, 2},
    {{"h", "l"}, 3},
    {{"ixh", "ixl"}, 4},
    {{"iyh", "iyl"}, 5},
};

} // namespace targets
} // namespace clang

using namespace clang;
using namespace clang::targets;

namespace {

struct Z80AsmReg {
  StringRef Name;
  unsigned SpellingLength;
};

/// Returns the asm register name for the given spelling, or an empty string if
/// the spelling is not a known register.
static StringRef normalizeZ80RegisterSpelling(StringRef Spelling) {
  auto EqualsLower = [&](StringRef LowerName) {
    if (Spelling.size() != LowerName.size())
      return false;
    for (size_t I = 0, E = LowerName.size(); I != E; ++I)
      if (std::tolower(static_cast<unsigned char>(Spelling[I])) !=
          LowerName[I])
        return false;
    return true;
  };

  if (EqualsLower("a"))
    return "a";
  if (EqualsLower("bc"))
    return "bc";
  if (EqualsLower("b"))
    return "b";
  if (EqualsLower("c"))
    return "c";
  if (EqualsLower("de"))
    return "de";
  if (EqualsLower("d"))
    return "d";
  if (EqualsLower("e"))
    return "e";
  if (EqualsLower("hl"))
    return "hl";
  if (EqualsLower("h"))
    return "h";
  if (EqualsLower("l"))
    return "l";
  if (EqualsLower("ixh") || EqualsLower("xh"))
    return "ixh";
  if (EqualsLower("ixl") || EqualsLower("xl"))
    return "ixl";
  if (EqualsLower("ix") || EqualsLower("x"))
    return "ix";
  if (EqualsLower("iyh") || EqualsLower("yh"))
    return "iyh";
  if (EqualsLower("iyl") || EqualsLower("yl"))
    return "iyl";
  if (EqualsLower("iy") || EqualsLower("y"))
    return "iy";
  if (EqualsLower("sps"))
    return "sps";
  if (EqualsLower("spl"))
    return "spl";
  if (EqualsLower("sp"))
    return "sp";
  return "";
}

static std::optional<Z80AsmReg>
matchZ80AsmRegister(StringRef Constraint, const Z80TargetInfoBase &Target,
                    bool Canonicalize) {
  bool HasBraces = Constraint.consume_front("{");
  if (HasBraces) {
    size_t BraceEnd = Constraint.find('}');
    if (BraceEnd == StringRef::npos)
      return std::nullopt;
    Constraint = Constraint.take_front(BraceEnd);
  }

  if (Constraint.empty())
    return std::nullopt;

  unsigned MaxLen = Constraint.size();
  if (MaxLen > 3)
    MaxLen = 3;

  for (unsigned Len = MaxLen; Len != 0; --Len) {
    StringRef Candidate = Constraint.take_front(Len);
    StringRef RegName = normalizeZ80RegisterSpelling(Candidate);
    if (RegName.empty())
      continue;

    if (!Target.isValidGCCRegisterName(RegName))
      continue;

    if (Canonicalize)
      RegName = Target.getNormalizedGCCRegisterName(RegName, true);

    unsigned SpellingLength = Len + (HasBraces ? 2 : 0);
    return Z80AsmReg{RegName, SpellingLength};
  }

  return std::nullopt;
}

} // namespace

static unsigned matchAsmCCConstraint(const char *&Name) {
  return llvm::StringSwitch<unsigned>(Name)
      .Case("@ccnz", 5)
      .Case("@ccz", 4)
      .Case("@ccnc", 5)
      .Case("@ccc", 4)
      .Case("@ccpo", 5)
      .Case("@ccpe", 5)
      .Case("@ccp", 4)
      .Case("@ccm", 4)
      .Default(0);
}

void Z80TargetInfoBase::getTargetDefines(const LangOptions &Opts,
                                         MacroBuilder &Builder) const {
  // Inline assembly supports Z80 flag outputs.
  Builder.defineMacro("__GCC_ASM_FLAG_OUTPUTS__");
}

StringRef Z80TargetInfoBase::getConstraintRegister(StringRef Constraint,
                                                   StringRef Expression) const {
  Constraint =
      Constraint.drop_until(
          [](char C) { return isalpha(C) || C == '{' || C == '@'; });
  if (Constraint.empty())
    return "";

  if (Constraint.front() == 'r' || Constraint.front() == 'R')
    return Expression;

  if (auto Reg = matchZ80AsmRegister(Constraint, *this, /*Canonicalize=*/true))
    return Reg->Name;

  return "";
}

bool Z80TargetInfoBase::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  if (auto Reg = matchZ80AsmRegister(StringRef(Name), *this,
                                     /*Canonicalize=*/false)) {
    Name += Reg->SpellingLength - 1;
    Info.setAllowsRegister();
    return true;
  }

  switch (Name[0]) {
  case 'I': // bit offset within byte [0,7]
    Info.setRequiresImmediate(0, 7);
    return true;
  case 'J': // immediate [0,0xFF]
    Info.setRequiresImmediate(0, 0xFF);
    return true;
  case 'K': // immediate [0,0xFFFF]
    Info.setRequiresImmediate(0, 0xFFFF);
    return true;
  case 'L': // immediate [0,0xFFFFFF]
    Info.setRequiresImmediate(0, 0xFFFFFF);
    return true;
  case 'M': // im mode [0,2]
    Info.setRequiresImmediate(0, 2);
    return true;
  case 'N': // rst target [0,7]<<3
    Info.setRequiresImmediate(
        {0 << 3, 1 << 3, 2 << 3, 3 << 3, 4 << 3, 5 << 3, 6 << 3, 7 << 3});
    return true;
  case 'O': // signed offset [-128,127]
    Info.setRequiresImmediate(-128, 127);
    return true;
  case 'R': // reg including index
    Info.setAllowsRegister();
    return true;
  case '@':
    // CC condition changes.
    if (unsigned Len = matchAsmCCConstraint(Name)) {
      Name += Len - 1;
      Info.setAllowsRegister();
      return true;
    }
    break;
  }
  return false;
}

std::string
Z80TargetInfoBase::convertConstraint(const char *&Constraint) const {
  if (auto Reg = matchZ80AsmRegister(StringRef(Constraint), *this,
                                     /*Canonicalize=*/false)) {
    Constraint += Reg->SpellingLength - 1;
    return "{" + Reg->Name.str() + "}";
  }

  if (unsigned Len = matchAsmCCConstraint(Constraint)) {
    std::string Converted = "{" + std::string(Constraint, Len) + "}";
    Constraint += Len - 1;
    return Converted;
  }

  return std::string(1, Constraint[0]);
}

ArrayRef<TargetInfo::AddlRegName> Z80TargetInfoBase::getGCCAddlRegNames() const {
  return llvm::ArrayRef(AddlRegNames);
}

bool Z80TargetInfo::setCPU(const std::string &Name) {
  return llvm::StringSwitch<bool>(Name)
    .Case("generic", true)
    .Case("z80",     true)
    .Case("z180",    true)
    .Default(false);
}

bool Z80TargetInfo::
initFeatureMap(llvm::StringMap<bool> &Features,
               DiagnosticsEngine &Diags, StringRef CPU,
               const std::vector<std::string> &FeaturesVec) const {
  if (CPU == "z80")
    Features["undoc"] = true;
  if (CPU == "z180")
    Features["z180"] = true;
  return TargetInfo::initFeatureMap(Features, Diags, CPU, FeaturesVec);
}

void Z80TargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Z80TargetInfoBase::getTargetDefines(Opts, Builder);
  defineCPUMacros(Builder, "z80", /*Tuning=*/false);
  Builder.defineMacro("_Z80");
  if (getTargetOpts().CPU == "undoc") {
    defineCPUMacros(Builder, "z80_undoc", /*Tuning=*/false);
    Builder.defineMacro("_Z80_UNDOC");
  } else if (getTargetOpts().CPU == "z180") {
    defineCPUMacros(Builder, "z180", /*Tuning=*/false);
    Builder.defineMacro("_Z180");
  }
}

llvm::SmallVector<Builtin::InfosShard>
Z80TargetInfo::getTargetBuiltins() const {
  return {};
}

ArrayRef<const char *> Z80TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(Z80GCCRegNames);
}

bool EZ80TargetInfo::setCPU(const std::string &Name) {
  return llvm::StringSwitch<bool>(Name)
    .Case("generic", true)
    .Case("ez80",    true)
    .Default(false);
}

void EZ80TargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  Z80TargetInfoBase::getTargetDefines(Opts, Builder);
  defineCPUMacros(Builder, "ez80", /*Tuning=*/false);
  Builder.defineMacro("_EZ80");
}

llvm::SmallVector<Builtin::InfosShard>
EZ80TargetInfo::getTargetBuiltins() const {
  return {};
}

ArrayRef<const char *> EZ80TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(EZ80GCCRegNames);
}
