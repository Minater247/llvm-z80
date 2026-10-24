//===-- Z80LateCopyPropagation.cpp - Reuse physical byte values
//------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Pseudo expansion and bank-region allocation can leave repeated byte moves.
// Track value identity in the physical byte registers, including exchanges,
// without choosing new registers or introducing transfers. The state
// has fixed size and is discarded at every basic-block boundary.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/MathExtras.h"
#include <array>

using namespace llvm;
#define DEBUG_TYPE "z80-late-copy"
STATISTIC(NumRedundantMoves, "Redundant physical Z80 byte moves removed");

namespace {
class Z80LateCopyPropagation final : public MachineFunctionPass {
public:
  static char ID;
  Z80LateCopyPropagation() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "Z80 late byte copy reuse"; }
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace

bool Z80LateCopyPropagation::runOnMachineFunction(MachineFunction &MF) {
  const auto &TRI = *MF.getSubtarget<Z80Subtarget>().getRegisterInfo();
  // Alternate pair bytes have no independently allocatable register names.
  // Keep their identities separately, so two EXX instructions restore the
  // primary values even if the intervening region changes the active bank.
  static constexpr MCPhysReg Regs[] = {Z80::B,   Z80::C,   Z80::D,  Z80::E,
                                       Z80::H,   Z80::L,   Z80::A,  Z80::IXH,
                                       Z80::IXL, Z80::IYH, Z80::IYL};
  static constexpr MCPhysReg AltPairs[] = {Z80::UBC_ALT, Z80::UDE_ALT,
                                           Z80::UHL_ALT};
  // Physical aliases do not depend on function attributes or word mode.
  // Cache their byte masks once instead of querying every byte at every MI.
  static const auto ClobberMasks = [&] {
    std::array<unsigned, Z80::NUM_TARGET_REGS> Masks{};
    for (unsigned R = 1; R != Masks.size(); ++R) {
      for (unsigned B = 0; B != std::size(Regs); ++B)
        if (TRI.regsOverlap(R, Regs[B]))
          Masks[R] |= 1u << B;
      for (unsigned P = 0; P != std::size(AltPairs); ++P)
        if (TRI.regsOverlap(R, AltPairs[P]))
          Masks[R] |= 3u << (11 + 2 * P);
      if (TRI.regsOverlap(R, Z80::AF_ALT))
        Masks[R] |= 1u << 17;
    }
    return Masks;
  }();
  auto Index = [](Register R) {
    for (unsigned I = 0; I != std::size(Regs); ++I)
      if (R == Regs[I])
        return I;
    return unsigned(std::size(Regs));
  };
  bool Changed = false;
  for (auto &MBB : MF) {
    std::array<uint64_t, 18> Values;
    uint64_t Next = 0;
    auto Reset = [&] {
      for (auto &V : Values)
        V = Next++;
    };
    Reset();
    bool BlockChanged = false;
    for (auto I = MBB.begin(); I != MBB.end();) {
      auto &MI = *I++;
      if (MI.isDebugInstr())
        continue;
      if (MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
          MI.isTerminator() || MI.isCFIInstruction()) {
        Reset();
        continue;
      }
      unsigned Opc = MI.getOpcode();
      if (Opc == Z80::EXX16 || Opc == Z80::EXX24) {
        for (unsigned R = 0; R != 6; ++R)
          std::swap(Values[R], Values[11 + R]);
        continue;
      }
      if (Opc == Z80::EXAF) {
        std::swap(Values[6], Values[17]);
        continue;
      }
      if (Opc == Z80::EX16DE || Opc == Z80::EX24DE) {
        std::swap(Values[2], Values[4]);
        std::swap(Values[3], Values[5]);
        continue;
      }
      if (Opc == Z80::LD8gg || Opc == Z80::LD8xx || Opc == Z80::LD8yy) {
        unsigned Dst = Index(MI.getOperand(0).getReg());
        unsigned Src = Index(MI.getOperand(1).getReg());
        if (Dst != std::size(Regs) && Src != std::size(Regs) &&
            !MI.getOperand(1).isUndef()) {
          if (Values[Dst] == Values[Src] &&
              !MI.getFlag(MachineInstr::FrameSetup) &&
              !MI.getFlag(MachineInstr::FrameDestroy)) {
            MI.eraseFromParent();
            BlockChanged = true;
            ++NumRedundantMoves;
          } else {
            Values[Dst] = Values[Src];
          }
          // A word COPY's final byte move may carry an implicit full-pair
          // definition. That is a liveness marker, not another hardware write.
          continue;
        }
      }
      if (MI.hasUnmodeledSideEffects()) {
        Reset();
        continue;
      }
      unsigned Clobbered = 0;
      for (const auto &MO : MI.operands()) {
        if (MO.isRegMask())
          Clobbered = (1u << Values.size()) - 1;
        else if (MO.isReg() && MO.isDef())
          Clobbered |= ClobberMasks[MO.getReg()];
      }
      while (Clobbered) {
        Values[llvm::countr_zero(Clobbered)] = Next++;
        Clobbered &= Clobbered - 1;
      }
    }
    if (BlockChanged) {
      // The retained physical value now survives an earlier last-use hint.
      for (auto &MI : MBB)
        for (auto &MO : MI.operands())
          if (MO.isReg()) {
            if (MO.isDef())
              MO.setIsDead(false);
            else
              MO.setIsKill(false);
          }
      Changed = true;
    }
  }
  return Changed;
}

char Z80LateCopyPropagation::ID = 0;
INITIALIZE_PASS(Z80LateCopyPropagation, DEBUG_TYPE, "Z80 late byte copy reuse",
                false, false)
FunctionPass *llvm::createZ80LateCopyPropagation() {
  return new Z80LateCopyPropagation();
}
