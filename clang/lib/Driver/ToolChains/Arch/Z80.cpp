//===--- Z80.cpp - Z80 Helpers for Tools ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "clang/Driver/Driver.h"
#include "clang/Options/Options.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

const char *z80::getZ80TargetCPU(const ArgList &Args,
                                 const llvm::Triple &Triple) {
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ))
    return A->getValue();

  // Select the default CPU if none was given (or detection failed).

  if (Triple.getArch() != llvm::Triple::z80 &&
      Triple.getArch() != llvm::Triple::ez80)
    return nullptr; // This routine is only handling z80 targets.

  bool Is24Bit = Triple.getArch() == llvm::Triple::ez80;

  // Everything else goes to ez80 in 24-bit mode.
  if (Is24Bit)
    return "ez80";

  // Fallback to generic
  return "z80";
}

void z80::getZ80TargetFeatures(const Driver &D, const llvm::Triple &Triple,
                               const ArgList &Args,
                               std::vector<StringRef> &Features) {
  (void)D;
  (void)Triple;
  if (const Arg *A = Args.getLastArg(options::OPT_malternate_registers,
                                     options::OPT_mno_alternate_registers))
    Features.push_back(A->getOption().matches(options::OPT_malternate_registers)
                           ? "+alternate-regs"
                           : "-alternate-regs");
  if (const Arg *A = Args.getLastArg(options::OPT_mexpnoints,
                                   options::OPT_mno_expnoints))
    Features.push_back(A->getOption().matches(options::OPT_mexpnoints)
                           ? "+expnoints"
                           : "-expnoints");

}
