//===-- Z80LateCopyPropagation.cpp - Reuse physical byte values
//------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Pseudo expansion and bank-region allocation can leave repeated byte moves.
// Track value identity in physical byte registers, including exchanges, to
// reuse copies and cheaper memory addresses. The state has fixed size and is
// discarded at every basic-block boundary.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/MathExtras.h"
#include <array>

using namespace llvm;
#define DEBUG_TYPE "z80-late-copy"
STATISTIC(NumRedundantMoves, "Redundant physical Z80 byte moves removed");
STATISTIC(NumAddressReuses,
          "Z80 memory accesses using an equivalent primary pair");
STATISTIC(NumUnusedMoves, "Unused physical Z80 byte moves removed");

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
  const auto &ST = MF.getSubtarget<Z80Subtarget>();
  const auto &TRI = *ST.getRegisterInfo();
  const auto &TII = *ST.getInstrInfo();
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
    bool BlockChanged = false;
    // Removing an address copy can preserve an index value across the next
    // access. One bounded revisit folds those newly redundant byte moves.
    for (unsigned Pass = 0; Pass != 2; ++Pass) {
      std::array<uint64_t, 18> Values;
      uint64_t Next = 0;
      auto Reset = [&] {
        for (auto &V : Values)
          V = Next++;
      };
      Reset();
      bool AddressChanged = false;
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
            // definition. That is a liveness marker, not another hardware
            // write.
            continue;
          }
        }
        if (MI.hasUnmodeledSideEffects()) {
          Reset();
          continue;
        }
        // An indexed byte access can use an identical primary pair directly.
        // BC and DE address only accumulator loads/stores; HL addresses every
        // general byte register. Keep the memory operation and its ordering in
        // place. The byte identities prove only 16-bit addresses, so ADL mode
        // retains its original address register.
        if (ST.is16Bit()) {
          bool Load =
              Opc == Z80::LD8ap || Opc == Z80::LD8gp || Opc == Z80::LD8go;
          bool Store =
              Opc == Z80::LD8pa || Opc == Z80::LD8pg || Opc == Z80::LD8og;
          bool Offset = Opc == Z80::LD8go || Opc == Z80::LD8og;
          unsigned AddrOp = Load ? 1 : 0;
          if ((Load || Store) &&
              (!Offset || (MI.getOperand(AddrOp + 1).isImm() &&
                           MI.getOperand(AddrOp + 1).getImm() == 0))) {
            auto &Addr = MI.getOperand(AddrOp);
            unsigned High = Addr.getReg() == Z80::IX ? 7 : 9;
            if ((Addr.getReg() == Z80::IX || Addr.getReg() == Z80::IY) &&
                !Addr.isUndef()) {
              Register Byte = MI.getOperand(Load ? 0 : Offset ? 2 : 1).getReg();
              static constexpr MCPhysReg Pairs[] = {Z80::BC, Z80::DE, Z80::HL};
              for (unsigned P : {2u, 1u, 0u}) {
                if ((P != 2 && Byte != Z80::A) ||
                    Values[High] != Values[2 * P] ||
                    Values[High + 1] != Values[2 * P + 1])
                  continue;
                Addr.setReg(Pairs[P]);
                Addr.setIsKill(false);
                if (Offset)
                  MI.removeOperand(AddrOp + 1);
                MI.setDesc(
                    TII.get(Load ? (Byte == Z80::A ? Z80::LD8ap : Z80::LD8gp)
                                 : (Byte == Z80::A ? Z80::LD8pa : Z80::LD8pg)));
                AddressChanged = BlockChanged = true;
                ++NumAddressReuses;
                break;
              }
            }
          }
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
      if (AddressChanged) {
        // A reused address can leave the index-byte copies unused. Start with
        // every byte live at the block exit, preserving successor and ABI
        // state. Only explicit definitions kill liveness here: implicit
        // full-pair defs on expanded byte copies describe liveness rather than
        // hardware writes.
        unsigned Live = (1u << Values.size()) - 1;
        for (auto I = MBB.end(); I != MBB.begin();) {
          auto &MI = *--I;
          if (MI.isDebugInstr())
            continue;
          if (MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
              MI.isCFIInstruction() || MI.hasUnmodeledSideEffects()) {
            Live = (1u << Values.size()) - 1;
            continue;
          }
          unsigned Opc = MI.getOpcode();
          bool Copy =
              Opc == Z80::LD8gg || Opc == Z80::LD8xx || Opc == Z80::LD8yy;
          if (Copy) {
            unsigned Dst = ClobberMasks[MI.getOperand(0).getReg()];
            if (!(Live & Dst) && !MI.getFlag(MachineInstr::FrameSetup) &&
                !MI.getFlag(MachineInstr::FrameDestroy)) {
              ++I;
              MI.eraseFromParent();
              ++NumUnusedMoves;
              continue;
            }
            Live = (Live & ~Dst) | ClobberMasks[MI.getOperand(1).getReg()];
            continue;
          }
          for (const auto &MO : MI.operands())
            if (MO.isReg() && MO.isDef() && !MO.isImplicit())
              Live &= ~ClobberMasks[MO.getReg()];
          for (const auto &MO : MI.operands()) {
            if (MO.isRegMask())
              Live = (1u << Values.size()) - 1;
            else if (MO.isReg() && MO.isUse())
              Live |= ClobberMasks[MO.getReg()];
          }
        }
      }
      if (!AddressChanged)
        break;
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
