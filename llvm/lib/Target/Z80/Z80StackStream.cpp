//===-- Z80StackStream.cpp - Leaf input streams through SP ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Select POP for an initial word read in a straight-line, stack-free kernel.
// The environment must promise that no interrupt, including NMI, can occur.
// Entry adaptation and final SP restoration remain real instructions; callers
// must account for them when evaluating complete function-call performance.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"

using namespace llvm;

#define DEBUG_TYPE "z80-stack-stream"

namespace {
class Z80StackStream : public MachineFunctionPass {
public:
  static char ID;
  Z80StackStream() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "Z80 leaf stack streams"; }
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace

bool Z80StackStream::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<Z80Subtarget>();
  const Function &F = MF.getFunction();
  if ((!ST.expectNoInterrupts() && !F.hasFnAttribute("z80-expnoints")) ||
      !ST.is16Bit() || ST.hasEZ80Ops() || MF.size() != 1 ||
      F.getCallingConv() != CallingConv::C ||
      F.hasOptSize() || F.hasFnAttribute("interrupt") ||
      F.hasFnAttribute(Attribute::Naked) || MF.needsFrameMoves() ||
      MF.getFrameInfo().hasVarSizedObjects())
    return false;
  auto &MBB = MF.front();
  const auto &TII = *ST.getInstrInfo();
  const auto &TRI = *ST.getRegisterInfo();
  SmallVector<MachineInstr *, 32> Ops;
  for (auto &MI : MBB) {
    if (MI.isDebugInstr() || MI.isCFIInstruction() || Ops.size() == 64)
      return false;
    Ops.push_back(&MI);
  }
  if (Ops.size() < 6 || Ops.back()->getOpcode() != Z80::RET16)
    return false;
  unsigned Begin = 0, End = Ops.size() - 1;
  // The only accepted frame is an unused IX save. No locals, stack arguments,
  // variable allocation, inline assembly, calls, or internal exchanges via SP.
  if (Ops.front()->getOpcode() == Z80::PUSH16r &&
      Ops.front()->getOperand(0).getReg() == Z80::IX &&
      Ops[End - 1]->getOpcode() == Z80::POP16r &&
      Ops[End - 1]->getOperand(0).getReg() == Z80::IX) {
    ++Begin;
    --End;
  }
  if (MF.getFrameInfo().getStackSize() != 0 ||
      End < Begin + 5)
    return false;
  auto Move = [&](unsigned N, unsigned Opc, MCRegister Dst, MCRegister Src) {
    auto &MI = *Ops[N];
    return MI.getOpcode() == Opc && MI.getOperand(0).getReg() == Dst &&
           MI.getOperand(1).getReg() == Src;
  };
  // LD88rp through IY, expanded without scratch pushes:
  //   IYL <- low(ptr); IYH <- high(ptr); low(ptr) <- (IY);
  //   high(ptr) <- (IY+1). The word overwrites its dead input pointer.
  if (Ops[Begin]->getOpcode() != Z80::LD8yy)
    return false;
  MCRegister Low = Ops[Begin]->getOperand(1).getReg();
  MCRegister Pair = Low == Z80::C ? Z80::BC : Low == Z80::E ? Z80::DE : 0;
  if (!Pair)
    return false;
  MCRegister High = TRI.getSubReg(Pair, Z80::sub_high);
  if (!Move(Begin, Z80::LD8yy, Z80::IYL, Low) ||
      !Move(Begin + 1, Z80::LD8yy, Z80::IYH, High) ||
      !Move(Begin + 2, Z80::LD8gp, Low, Z80::IY) ||
      !Move(Begin + 3, Z80::LD8go, High, Z80::IY) ||
      Ops[Begin + 3]->getOperand(2).getImm() != 1 ||
      !Ops[Begin + 2]->hasOneMemOperand() ||
      !Ops[Begin + 3]->hasOneMemOperand())
    return false;
  auto *LowMMO = *Ops[Begin + 2]->memoperands_begin();
  auto *MMO = *Ops[Begin + 3]->memoperands_begin();
  for (auto *Byte : {LowMMO, MMO})
    if (!Byte->isUnordered() || Byte->isAtomic() || Byte->getAddrSpace() != 0 ||
        Byte->getSize() != LocationSize::precise(1))
      return false;
  // Entry setup may clobber A, L and flags. Each must be defined before its
  // first body use; H is explicitly preserved. IY holds the real SP to exit.
  bool ADefined = false, LDefined = false, FDefined = false;
  for (unsigned N = Begin + 4; N < Ops.size(); ++N) {
    auto &MI = *Ops[N];
    if (N == End && Begin) // paired IX restore
      continue;
    if (MI.isInlineAsm() || MI.isCall() || MI.isBranch() ||
        (MI.hasUnmodeledSideEffects() && N != Ops.size() - 1) ||
        (MI.isTerminator() && N != Ops.size() - 1))
      return false;
    for (const auto &MO : MI.operands()) {
      if (!MO.isReg() || !MO.getReg())
        continue;
      MCRegister R = MO.getReg();
      if (TRI.regsOverlap(R, Z80::IX) || TRI.regsOverlap(R, Z80::IY) ||
          (TRI.regsOverlap(R, Z80::SPS) && N != Ops.size() - 1))
        return false;
    }
    if ((!ADefined && MI.readsRegister(Z80::A, &TRI)) ||
        (!LDefined && MI.readsRegister(Z80::L, &TRI)) ||
        (!FDefined && MI.readsRegister(Z80::F, &TRI)))
      return false;
    ADefined |= MI.definesRegister(Z80::A, &TRI);
    // An implicit pair definition on a high-byte move is a liveness marker,
    // not a hardware write of the low byte.
    for (const auto &MO : MI.defs())
      LDefined |= MO.isReg() && !MO.isImplicit() &&
                  (MO.getReg() == Z80::L || MO.getReg() == Z80::HL);
    FDefined |= MI.definesRegister(Z80::F, &TRI);
  }
  auto Where = Ops[Begin]->getIterator();
  auto DL = Ops[Begin]->getDebugLoc();
  BuildMI(MBB, Where, DL, TII.get(Z80::LD16ri), Z80::IY).addImm(0);
  BuildMI(MBB, Where, DL, TII.get(Z80::ADD16as), Z80::IY).addReg(Z80::IY);
  BuildMI(MBB, Where, DL, TII.get(Z80::LD8gg), Z80::A).addReg(Z80::H);
  BuildMI(MBB, Where, DL, TII.get(Z80::LD8gg), Z80::H).addReg(High);
  BuildMI(MBB, Where, DL, TII.get(Z80::LD8gg), Z80::L).addReg(Low);
  BuildMI(MBB, Where, DL, TII.get(Z80::LD16sa)).addReg(Z80::HL);
  BuildMI(MBB, Where, DL, TII.get(Z80::LD8gg), Z80::H).addReg(Z80::A);
  BuildMI(MBB, Where, DL, TII.get(Z80::POP16r), Pair)
      .addMemOperand(LowMMO).addMemOperand(MMO);
  BuildMI(MBB, Ops[End]->getIterator(), DL, TII.get(Z80::LD16sa)).addReg(Z80::IY);
  for (unsigned N = Begin; N != Begin + 4; ++N)
    Ops[N]->eraseFromParent();
  // IX is untouched, and the checks above excluded unwind information and
  // every frame reference. The inherited empty-frame save is redundant.
  if (Begin) {
    Ops.front()->eraseFromParent();
    Ops[End]->eraseFromParent();
  }
  // Inserting setup and moving the word definition invalidates kill/dead hints.
  for (auto &MI : MBB)
    for (auto &MO : MI.operands())
      if (MO.isReg()) {
        if (MO.isUse())
          MO.setIsKill(false);
        else
          MO.setIsDead(false);
      }
  return true;
}

char Z80StackStream::ID = 0;
INITIALIZE_PASS(Z80StackStream, DEBUG_TYPE, "Z80 leaf stack streams", false, false)
FunctionPass *llvm::createZ80StackStream() { return new Z80StackStream(); }
