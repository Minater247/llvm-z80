//===- llvm/lib/Target/Z80/Z80InlineAsmLowering.cpp - Inline asm lowering -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the lowering from LLVM IR inline asm to MIR INLINEASM
//
//===----------------------------------------------------------------------===//

#include "Z80InlineAsmLowering.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80ISelLowering.h"
#include "Z80InstrInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Constants.h"
using namespace llvm;

#define DEBUG_TYPE "z80-inline-asm-lowering"

Z80InlineAsmLowering::Z80InlineAsmLowering(const Z80TargetLowering &TLI)
    : InlineAsmLowering(&TLI) {}

bool Z80InlineAsmLowering::lowerAsmOperandForConstraint(
    Value *Val, StringRef Constraint, std::vector<MachineOperand> &Ops,
    MachineIRBuilder &MIRBuilder) const {
  if (Constraint.size() == 1)
    if (auto *CI = dyn_cast<ConstantInt>(Val)) {
      const APInt &V = CI->getValue();
      switch (Constraint.front()) {
      case 'I':
        if (V.isIntN(3)) {
          Ops.push_back(MachineOperand::CreateImm(V.getZExtValue()));
          return true;
        }
        break;
      case 'J':
        if (V.isIntN(8)) {
          Ops.push_back(MachineOperand::CreateImm(V.getZExtValue()));
          return true;
        }
        break;
      case 'M':
        if (V.ule(2)) {
          Ops.push_back(MachineOperand::CreateImm(V.getZExtValue()));
          return true;
        }
        break;
      case 'N':
        if (V.isIntN(6) && (V.getZExtValue() & 7) == 0) {
          Ops.push_back(MachineOperand::CreateImm(V.getZExtValue()));
          return true;
        }
        break;
      case 'O':
        if (V.isSignedIntN(8)) {
          Ops.push_back(MachineOperand::CreateImm(V.getSExtValue()));
          return true;
        }
        break;
      default:
        break;
      }
    }

  return InlineAsmLowering::lowerAsmOperandForConstraint(Val, Constraint, Ops,
                                                         MIRBuilder);
}
