//=== lib/Target/Z80/Z80MachinePreRAOptimization.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before register allocation.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/DenseSet.h"
#include <optional>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "z80-machine-prera-opt"

using namespace llvm;

namespace {
/// Expose whole-pair bank choices after copy coalescing and before allocation.
/// Byte accesses retain primary operands; completed pairs can move to a bank.
static bool selectBankInstructions(MachineFunction &MF) {
  bool changed = false;
  auto &MRI = MF.getRegInfo();
  const auto *TII = MF.getSubtarget().getInstrInfo();
  const auto &ST = MF.getSubtarget<Z80Subtarget>();
  if (!ST.useAlternateRegs() || !ST.is16Bit() || ST.hasEZ80Ops() ||
      MF.getFunction().hasFnAttribute("interrupt") ||
      MF.getFunction().hasFnAttribute(Attribute::Naked) ||
      MF.getFunction().getCallingConv() == CallingConv::PreserveAll)
    return changed;
  for (const auto &MBB : MF)
    for (const auto &MI : MBB)
      if (MI.isInlineAsm() && MI.getOperand(0).getSymbolName()[0])
        return changed;

  // Whole-pair pointer chains can stay in either bank. Subregister consumers
  // and instructions requiring a primary pair keep their existing classes.
  SmallDenseSet<Register, 32> Bankable;
  std::optional<MachineDominatorTree> Dominators;
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register R = Register::index2VirtReg(I);
    if (MRI.reg_nodbg_empty(R) ||
        !Z80::R16RegClass.hasSubClassEq(MRI.getRegClass(R)))
      continue;
    bool Eligible = true, HasBankOperation = false;
    SmallVector<MachineOperand *, 2> ByteDefs;
    SmallVector<MachineOperand *, 2> ByteUses;
    bool WholeCopy = false;
    bool WholeDef = false;
    for (auto &MO : MRI.reg_nodbg_operands(R)) {
      const auto &MI = *MO.getParent();
      if (MI.isBundled()) {
        Eligible = false;
        break;
      }
      unsigned Opc = MI.getOpcode();
      if (MO.getSubReg()) {
        if (MO.isDef() &&
            (MO.getSubReg() == Z80::sub_low ||
             MO.getSubReg() == Z80::sub_high) &&
            (MI.isCopy() || Opc == Z80::LD8ri || Opc == Z80::LD8r0) &&
            ByteDefs.size() < 2) {
          ByteDefs.push_back(&MO);
          continue;
        }
        if (MO.isUse() && MI.isCopy() &&
            (MO.getSubReg() == Z80::sub_low ||
             MO.getSubReg() == Z80::sub_high)) {
          ByteUses.push_back(&MO);
          continue;
        }
        Eligible = false;
        break;
      }
      WholeDef |= MO.isDef();
      if (MI.isCopy()) {
        const auto &Other = MI.getOperand(MO.isDef() ? 1 : 0);
        Register CopyReg = Other.getReg();
        WholeCopy |= MO.isUse() && CopyReg.isVirtual();
        if (Other.getSubReg() ||
            (CopyReg.isPhysical()
                 ? !Z80::R16RegClass.contains(CopyReg)
                 : !Z80::R16RegClass.hasSubClassEq(MRI.getRegClass(CopyReg)))) {
          Eligible = false;
          break;
        }
      }
      HasBankOperation |= Opc == Z80::ADD16rr || Opc == Z80::INC16r ||
                          Opc == Z80::DEC16r || Opc == Z80::LD8ap ||
                          Opc == Z80::LD8pa;
      if (Opc != TargetOpcode::COPY && Opc != Z80::LD16ri &&
          Opc != Z80::ADD16rr && Opc != Z80::INC16r && Opc != Z80::DEC16r &&
          Opc != Z80::LD8ap && Opc != Z80::LD8pa) {
        Eligible = false;
        break;
      }
    }
    if (!Eligible || (!HasBankOperation && !(WholeCopy && !ByteDefs.empty())) ||
        (ByteDefs.empty() && !ByteUses.empty()))
      continue;
    if (!ByteDefs.empty()) {
      if (WholeDef || ByteDefs.size() != 2 ||
          ByteDefs[0]->getSubReg() == ByteDefs[1]->getSubReg() ||
          ByteDefs[0]->getParent()->getParent() !=
              ByteDefs[1]->getParent()->getParent())
        continue;
      /* Local constructions have little opportunity to recover the extra
         transfer. Expose the choice for lifetimes extending beyond this
         block, where calls and loops can otherwise force repeated spills. */
      auto *DefBlock = ByteDefs[0]->getParent()->getParent();
      if (llvm::none_of(MRI.use_nodbg_operands(R), [&](const MachineOperand &MO) {
            return !MO.getSubReg() &&
                   MO.getParent()->getParent() != DefBlock;
          }))
        continue;
      if (!Dominators)
        Dominators.emplace(MF);
      if (Dominators->dominates(ByteDefs[1]->getParent(),
                                ByteDefs[0]->getParent()))
        std::swap(ByteDefs[0], ByteDefs[1]);
      auto *Last = ByteDefs[1]->getParent();
      if (!ByteDefs[0]->isUndef() || ByteDefs[1]->isUndef() ||
          llvm::any_of(MRI.use_nodbg_operands(R), [&](const MachineOperand &MO) {
            return !MO.getSubReg() &&
                   !Dominators->dominates(Last, MO.getParent());
          }))
        continue;
      /* Byte assembly requires the primary bank, but that constraint ends
         once both lanes are defined. Separate the construction from the
         whole-pair lifetime so a long-lived address can reside in either
         bank. The allocator can coalesce the copy when it stays primary. */
      Register Built = MRI.createVirtualRegister(MRI.getRegClass(R));
      for (auto *MO : ByteDefs)
        MO->setReg(Built);
      for (auto *MO : ByteUses)
        MO->setReg(Built);
      BuildMI(*Last->getParent(), std::next(Last->getIterator()),
              Last->getDebugLoc(), TII->get(TargetOpcode::COPY), R)
          .addReg(Built);
      changed = true;
    }
    Bankable.insert(R);
  }
  // A tied update must admit the same bank on both sides. Propagate rejected
  // endpoints through the chain before widening any register class.
  SmallVector<Register, 16> Rejected;
  auto Reject = [&](Register R) {
    if (Bankable.erase(R))
      Rejected.push_back(R);
  };
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      if (MI.getOpcode() == Z80::INC16r || MI.getOpcode() == Z80::DEC16r) {
        Register Dst = MI.getOperand(0).getReg(),
                 Src = MI.getOperand(1).getReg();
        if (!Bankable.contains(Dst) || !Bankable.contains(Src)) {
          Reject(Dst);
          Reject(Src);
        }
      }
  while (!Rejected.empty()) {
    Register R = Rejected.pop_back_val();
    for (const auto &MI : MRI.reg_nodbg_instructions(R))
      if (MI.getOpcode() == Z80::INC16r || MI.getOpcode() == Z80::DEC16r) {
        Reject(MI.getOperand(0).getReg());
        Reject(MI.getOperand(1).getReg());
      }
  }
  // A value used only as the right input of primary-only adds would need
  // a saved scratch pair at every use. Retain its primary class so spilling
  // and rematerialization can provide the cheaper native input directly.
  SmallVector<Register, 16> PrimaryInputs;
  for (Register R : Bankable) {
    bool HasBankUse = false;
    for (const auto &MO : MRI.reg_nodbg_operands(R)) {
      const auto &MI = *MO.getParent();
      if (MI.getOpcode() == Z80::ADD16rr) {
        unsigned Op = &MO - &MI.getOperand(0);
        HasBankUse |= Op != 2 || Bankable.contains(MI.getOperand(0).getReg());
      } else if (MI.isCopy()) {
        HasBankUse |= MO.isUse() &&
                      Bankable.contains(MI.getOperand(0).getReg());
      } else if (MI.getOpcode() != Z80::LD16ri)
        HasBankUse = true;
    }
    if (!HasBankUse)
      PrimaryInputs.push_back(R);
  }
  for (Register R : PrimaryInputs)
    Bankable.erase(R);

  for (Register R : Bankable)
    MRI.setRegClass(R, &Z80::B16RegClass);
  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      unsigned Opc = MI.getOpcode(), NewOpc = 0, Op = 0;
      if (Opc == Z80::ADD16rr) {
        if (Bankable.contains(MI.getOperand(0).getReg()) ||
            Bankable.contains(MI.getOperand(1).getReg()) ||
            Bankable.contains(MI.getOperand(2).getReg())) {
          MI.setDesc(TII->get(Z80::BANK_ADD16));
          Register Src = MI.getOperand(2).getReg();
          if (Src.isVirtual())
            MRI.constrainRegClass(Src, &Z80::BO16RegClass);
          changed = true;
        }
        continue;
      }
      switch (Opc) {
      case Z80::LD16ri:
        NewOpc = Z80::BANK_LD16ri;
        break;
      case Z80::INC16r:
        NewOpc = Z80::BANK_INC16;
        break;
      case Z80::DEC16r:
        NewOpc = Z80::BANK_DEC16;
        break;
      case Z80::LD8ap:
        NewOpc = Z80::BANK_LD8ap;
        Op = 1;
        break;
      case Z80::LD8pa:
        NewOpc = Z80::BANK_LD8pa;
        break;
      }
      if (NewOpc && Bankable.contains(MI.getOperand(Op).getReg())) {
        MI.setDesc(TII->get(NewOpc));
        changed = true;
      }
    }
  return changed;
}

/// Allocate independently changing byte lanes between word uses. The selected
/// stable lane can hold a constant or a value defined once across blocks;
/// constants are rematerialized at their uses. Word definitions update both
/// lanes, preserving carries and replacements. Memory operations stay in place.
static bool separateStableLanes(MachineFunction &MF, unsigned StableSub) {
  unsigned VariableSub = StableSub == Z80::sub_high ? Z80::sub_low : Z80::sub_high;
  if (!MF.getSubtarget<Z80Subtarget>().is16Bit())
    return false;
  auto &MRI = MF.getRegInfo();
  const auto &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  DenseMap<std::pair<Register, unsigned>, std::optional<int64_t>> Constants;
  auto Constant = [&](auto &&Self, Register R, unsigned Sub,
                      unsigned Depth) -> std::optional<int64_t> {
    if (!R.isVirtual() || Depth == 8)
      return std::nullopt;
    auto Key = std::make_pair(R, Sub);
    auto Found = Constants.find(Key);
    if (Found != Constants.end())
      return Found->second;
    auto Compute = [&]() -> std::optional<int64_t> {
      std::optional<int64_t> Value;
      for (const auto &MO : MRI.def_operands(R)) {
        if (MO.getSubReg() != Sub) {
          if (!MO.getSubReg() || !Sub)
            return std::nullopt;
          continue;
        }
        const auto &MI = *MO.getParent();
        std::optional<int64_t> V;
        if (MI.getOpcode() == Z80::LD8r0)
          V = 0;
        else if (MI.getOpcode() == Z80::LD8ri && MI.getOperand(1).isImm())
          V = MI.getOperand(1).getImm();
        else if (MI.isCopy() && !MI.getOperand(1).isUndef())
          V = Self(Self, MI.getOperand(1).getReg(),
                   MI.getOperand(1).getSubReg(), Depth + 1);
        if (!V || (Value && (*Value & 255) != (*V & 255)))
          return std::nullopt;
        Value = *V & 255;
      }
      return Value;
    };
    auto Value = Compute();
    Constants[Key] = Value;
    return Value;
  };
  SmallVector<std::pair<Register, std::optional<int64_t>>, 16> Candidates;
  for (unsigned I = 0; I != MRI.getNumVirtRegs(); ++I) {
    Register R = Register::index2VirtReg(I);
    if (MRI.reg_nodbg_empty(R) ||
        !Z80::R16RegClass.hasSubClassEq(MRI.getRegClass(R)))
      continue;
    auto StableValue = Constant(Constant, R, StableSub, 0);
    bool Eligible = true, ShortUses = true, CrossBlock = false;
    bool StableUses = false, MultipleUseBlocks = false;
    const MachineBasicBlock *UseBlock = nullptr;
    const MachineBasicBlock *StableBlock = nullptr;
    unsigned VariableDefs = 0, StableDefs = 0, FullUses = 0;
    for (const auto &MO : MRI.def_operands(R)) {
      if (MO.getSubReg() == VariableSub)
        ++VariableDefs;
      else if (MO.getSubReg() == StableSub) {
        ++StableDefs;
        StableBlock = MO.getParent()->getParent();
      } else if (MO.getSubReg())
        Eligible = false;
    }
    if (!Eligible || !VariableDefs ||
        (!StableValue && (StableDefs != 1 || VariableDefs < 2)))
      continue;
    for (const auto &MO : MRI.reg_nodbg_operands(R)) {
      if (MO.getParent()->isBundled() || MO.getParent()->isInlineAsm()) {
        Eligible = false;
        break;
      }
      if (MO.isDef())
        continue;
      StableUses |= MO.getSubReg() == StableSub;
      if (!MO.getSubReg()) {
        Eligible &= !MO.isUndef();
        ++FullUses;
        if (UseBlock && UseBlock != MO.getParent()->getParent())
          MultipleUseBlocks = true;
        UseBlock = MO.getParent()->getParent();
        CrossBlock |= MO.getParent()->getParent() != StableBlock;
        if (!StableValue)
          continue;
        auto I = MO.getParent()->getIterator();
        auto Begin = MO.getParent()->getParent()->begin();
        bool NearbyDef = false;
        unsigned Distance = 0;
        while (I != Begin && Distance < 3) {
          --I;
          if (I->isDebugInstr())
            continue;
          ++Distance;
          for (const auto &Def : I->defs())
            NearbyDef |= Def.isReg() && Def.getReg() == R &&
                         Def.getSubReg() == VariableSub;
          if (NearbyDef)
            break;
        }
        ShortUses &= NearbyDef;
      } else if (MO.getSubReg() != VariableSub &&
                 MO.getSubReg() != StableSub)
        Eligible = false;
    }
    /* A nearby word use saves little live-register space, while rebuilding
       a simple loop-carried counter would reload its constant byte on
       every iteration. Keep those compact pair ranges intact. Separate shared
       constants used as individual bytes or by several blocks, where they
       would otherwise extend pair lifetimes and inhibit rematerialization.
       A nonconstant stable byte pays for separation when several word uses
       share its lifetime across blocks and variable-byte definitions. */
    bool Profitable =
        StableValue
            ? ((!ShortUses || MultipleUseBlocks || StableUses) &&
               !(VariableDefs > 1 && FullUses == 1 && !StableUses))
            : (StableDefs == 1 && VariableDefs > 1 && FullUses > 1 && CrossBlock);
    if (Eligible && VariableDefs && FullUses && Profitable)
      Candidates.emplace_back(R, StableValue);
  }
  for (auto [R, StableValue] : Candidates) {
    const auto *RC = MRI.getRegClass(R);
    /* Coalescing can tie one shared byte to unrelated variable-byte
       definitions. Rebuild only the pairs needed by word instructions, so
       the lanes can occupy or spill individual byte registers. */
    Register Variable = MRI.createVirtualRegister(&Z80::G8RegClass);
    Register Stable = MRI.createVirtualRegister(&Z80::G8RegClass);
    SmallVector<MachineOperand *, 16> Operands;
    for (auto &MO : MRI.reg_nodbg_operands(R))
      Operands.push_back(&MO);
    DenseMap<MachineInstr *, Register> Pairs;
    SmallPtrSet<MachineInstr *, 4> PairDefs;
    MRI.markUsesInDebugValueAsUndef(R);
    for (auto *MO : Operands) {
      if (unsigned Sub = MO->getSubReg()) {
        MO->setReg(Sub == VariableSub ? Variable : Stable);
        MO->setSubReg(0);
        if (MO->isDef())
          MO->setIsUndef(false);
      } else {
        MachineInstr &MI = *MO->getParent();
        Register &Pair = Pairs[&MI];
        if (!Pair) {
          Pair = MRI.createVirtualRegister(RC);
          bool ReadsPair =
              llvm::any_of(MI.operands(), [PairReg = R](const auto &Op) {
                return Op.isReg() && Op.getReg() == PairReg && Op.isUse() &&
                       !Op.getSubReg() && !Op.isUndef();
              });
          if (ReadsPair) {
            BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                    TII.get(TargetOpcode::COPY))
                .addReg(Pair, RegState::Define | RegState::Undef, VariableSub)
                .addReg(Variable);
            if (StableValue)
              BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                      TII.get(Z80::LD8ri))
                  .addReg(Pair, RegState::Define, StableSub)
                  .addImm(*StableValue);
            else
              BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                      TII.get(TargetOpcode::COPY))
                  .addReg(Pair, RegState::Define, StableSub)
                  .addReg(Stable);
          }
        }
        MO->setReg(Pair);
        if (MO->isDef()) {
          MO->setIsDead(false);
          PairDefs.insert(&MI);
        }
      }
    }
    /* A word operation may change both lanes between partial definitions.
       Publish its result back to the independent byte ranges, including the
       high byte when arithmetic carries out of the low byte. */
    for (MachineInstr *MI : PairDefs) {
      auto Insert = std::next(MI->getIterator());
      Register Pair = Pairs[MI];
      BuildMI(*MI->getParent(), Insert, MI->getDebugLoc(),
              TII.get(TargetOpcode::COPY), Variable)
          .addReg(Pair, 0, VariableSub);
      BuildMI(*MI->getParent(), Insert, MI->getDebugLoc(),
              TII.get(TargetOpcode::COPY), Stable)
          .addReg(Pair, 0, StableSub);
    }
    /* A range may originally require an index pair for an offset load. Its
       other word uses, such as call arguments, can use ordinary pairs once
       separated. Retaining the old class would add unnecessary transfers. */
    for (const auto &Entry : Pairs)
      MRI.recomputeRegClass(Entry.second);
    MRI.recomputeRegClass(Variable);
    SmallVector<MachineInstr *, 16> ConstantCopies;
    for (auto &MI : MRI.use_nodbg_instructions(Stable)) {
      if (!StableValue || !MI.isCopy())
        continue;
      const auto &Dst = MI.getOperand(0);
      Register D = Dst.getReg();
      bool ByteDestination =
          D.isPhysical()
              ? Z80::R8RegClass.contains(D)
              : (Dst.getSubReg()
                     ? Z80::R16RegClass.hasSubClassEq(MRI.getRegClass(D))
                     : Z80::R8RegClass.hasSubClassEq(MRI.getRegClass(D)));
      if (ByteDestination)
        ConstantCopies.push_back(&MI);
    }
    for (auto *MI : ConstantCopies) {
      MI->setDesc(TII.get(Z80::LD8ri));
      MI->getOperand(1).ChangeToImmediate(*StableValue);
    }
    MRI.recomputeRegClass(Stable);
    /* Rematerializing the stable byte can make its original definitions and
       their input copies dead. Remove those chains before they become tiny
       unspillable intervals competing with live values during allocation. */
    SmallVector<Register, 8> DeadInputs{Stable};
    while (!DeadInputs.empty()) {
      Register Dead = DeadInputs.pop_back_val();
      if (!MRI.use_nodbg_empty(Dead))
        continue;
      for (MachineInstr &MI :
           llvm::make_early_inc_range(MRI.def_instructions(Dead))) {
        if (!MI.isDead(MRI))
          continue;
        for (const auto &MO : MI.uses())
          if (MO.isReg() && MO.getReg().isVirtual())
            DeadInputs.push_back(MO.getReg());
        MI.eraseFromParent();
      }
    }
    Changed = true;
  }
  return Changed;
}

/// Give disconnected definitions independent intervals before allocation.
/// Lane separation can expose components formerly joined by a constant byte.
class Z80PrepareLiveRanges : public MachineFunctionPass {
public:
  static char ID;
  Z80PrepareLiveRanges() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "Z80 Prepare Live Ranges"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<SlotIndexesWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override {
    auto &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();
    auto &MRI = MF.getRegInfo();
    SmallVector<LiveInterval *, 8> Split;
    bool Changed = false;
    for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
      Register R = Register::index2VirtReg(I);
      if (LIS.hasInterval(R) && LIS.getInterval(R).getNumValNums() > 1) {
        Split.clear();
        LIS.splitSeparateComponents(LIS.getInterval(R), Split);
        Changed |= !Split.empty();
      }
    }
    return Changed;
  }
};

class Z80MachinePreRAOptimization : public MachineFunctionPass {
public:
  static char ID;

  Z80MachinePreRAOptimization() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Z80 Machine Pre-RA Optimization";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

bool Z80MachinePreRAOptimization::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallSet<Register, 16> FoldAsLoadDefCandidates;

  // Whole-pair updates in a terminating part of a live range need not tie
  // together byte lanes that vary independently in the preceding loop.
  // Give each such block its own pair; the incoming copy preserves its value.
  auto &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  SmallVector<std::pair<Register, MachineBasicBlock *>, 8> LocalPairs;
  if (MF.getSubtarget<Z80Subtarget>().is16Bit())
    for (unsigned N = 0; N != MRI.getNumVirtRegs(); ++N) {
      Register R = Register::index2VirtReg(N);
      if (!LIS.hasInterval(R) || MRI.reg_nodbg_empty(R) ||
          !Z80::R16RegClass.hasSubClassEq(MRI.getRegClass(R)))
        continue;
      unsigned HighDefs = 0, LowDefs = 0;
      SmallPtrSet<MachineBasicBlock *, 4> WholeDefs;
      for (auto &MO : MRI.def_operands(R)) {
        if (MO.getSubReg() == Z80::sub_high)
          ++HighDefs;
        else if (MO.getSubReg() == Z80::sub_low)
          ++LowDefs;
        else if (!MO.getSubReg())
          WholeDefs.insert(MO.getParent()->getParent());
      }
      if (HighDefs != 1 || LowDefs < 2)
        continue;
      const auto &LI = LIS.getInterval(R);
      for (auto *MBB : WholeDefs) {
        if (LIS.isLiveOutOfMBB(LI, MBB))
          continue;
        // A whole incoming copy requires both lanes. Partially live inputs
        // retain their original representation and undefined-lane semantics.
        if (LIS.isLiveInToMBB(LI, MBB) &&
            llvm::any_of(LI.subranges(), [&](const LiveInterval::SubRange &SR) {
              return !LIS.isLiveInToMBB(SR, MBB);
            }))
          continue;
        LocalPairs.emplace_back(R, MBB);
      }
    }
  llvm::sort(LocalPairs, [](const auto &A, const auto &B) {
    return A.first != B.first ? A.first < B.first
                             : A.second->getNumber() < B.second->getNumber();
  });
  for (auto [R, MBB] : LocalPairs) {
    Register Local = MRI.createVirtualRegister(MRI.getRegClass(R));
    bool LiveIn = LIS.isLiveInToMBB(LIS.getInterval(R), MBB);
    for (auto &MI : *MBB)
      for (auto &MO : MI.operands())
        if (MO.isReg() && MO.getReg() == R)
          MO.setReg(Local);
    if (LiveIn)
      BuildMI(*MBB, MBB->begin(), DebugLoc(), TII.get(TargetOpcode::COPY), Local)
          .addReg(R);
    MRI.markUsesInDebugValueAsUndef(R);
    Changed = true;
  }

  // PHI elimination and coalescing can join several constants into one range.
  // Materialize each constant after intervening calls when its first use is
  // later: a call need not force all the incoming values of that range to spill.
  for (auto &MBB : MF) {
    if (llvm::none_of(MBB, [](const MachineInstr &MI) { return MI.isCall(); }))
      continue;
    SmallVector<MachineInstr *, 64> Instructions;
    SmallVector<unsigned, 4> Calls;
    for (auto &MI : MBB) {
      if (MI.isCall())
        Calls.push_back(Instructions.size());
      Instructions.push_back(&MI);
    }
    /* Record each register's next access in one reverse walk. Scanning ahead
       separately for every constant repeats operand checks in large blocks,
       especially across inline assembly with many register operands. */
    DenseMap<Register, unsigned> NextAccess;
    unsigned NextBarrier = Instructions.size();
    SmallVector<std::pair<MachineInstr *, MachineInstr *>, 8> Moves;
    for (unsigned I = Instructions.size(); I-- != 0;) {
      auto &MI = *Instructions[I];
      if ((MI.getOpcode() == Z80::LD8ri || MI.getOpcode() == Z80::LD16ri ||
           MI.getOpcode() == Z80::LD24ri) &&
          MI.getNumOperands() == 2 && MI.getOperand(1).isImm() &&
          MI.getOperand(0).getReg().isVirtual() &&
          !MI.getOperand(0).getSubReg() && !MI.isBundled()) {
        Register R = MI.getOperand(0).getReg();
        unsigned Limit = std::min(I + 65, NextBarrier);
        auto Next = NextAccess.find(R);
        if (Next != NextAccess.end())
          Limit = std::min(Limit, Next->second);
        auto Call = llvm::lower_bound(Calls, Limit);
        if (Call != Calls.begin() && *--Call > I)
          Moves.emplace_back(&MI, Instructions[*Call]);
      }
      if (MI.isTerminator() || MI.isBundled() || MI.isDebugInstr())
        NextBarrier = I;
      for (const auto &MO : MI.operands())
        if (MO.isReg() && MO.getReg().isVirtual() &&
            (MO.isDef() || MO.readsReg()))
          NextAccess[MO.getReg()] = I;
    }
    for (auto [MI, Call] : llvm::reverse(Moves)) {
      MBB.splice(std::next(Call->getIterator()), &MBB, MI->getIterator());
      Changed = true;
    }
  }

  // Copy propagation can expose A after selection constrained a byte access
  // to HL/IX/IY. The accumulator forms also address BC and DE. Recover that
  // freedom before allocation, rather than copying a live pointer to an index
  // register just to store a result already in A.
  SmallSet<Register, 16> RelaxedPointers;
  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      unsigned PtrOp;
      if (MI.getOpcode() == Z80::LD8pg &&
          MI.getOperand(1).getReg() == Z80::A) {
        MI.setDesc(TII.get(Z80::LD8pa));
        PtrOp = 0;
      } else if (MI.getOpcode() == Z80::LD8gp &&
                 MI.getOperand(0).getReg() == Z80::A) {
        MI.setDesc(TII.get(Z80::LD8ap));
        PtrOp = 1;
      } else
        continue;
      Register Ptr = MI.getOperand(PtrOp).getReg();
      if (Ptr.isVirtual())
        RelaxedPointers.insert(Ptr);
      Changed = true;
    }
  for (Register Ptr : RelaxedPointers)
    MRI.recomputeRegClass(Ptr);

  for (MachineBasicBlock &MBB : MF) {
    for (auto MII = MBB.begin(), MIE = MBB.end(); MII != MIE;) {
      MachineInstr *MI = &*MII;
      ++MII;

      if (MI->canFoldAsLoad() && MI->mayLoad() &&
          MI->getNumExplicitDefs() == 1) {
        MachineOperand &DefMO = MI->getOperand(0);
        if (DefMO.isReg()) {
          Register DefReg = DefMO.getReg();
          if (DefReg.isVirtual() && !DefMO.getSubReg() &&
              MRI.hasOneNonDBGUser(DefReg)) {
            FoldAsLoadDefCandidates.insert(DefReg);
            continue;
          }
        }
      }

      if (!FoldAsLoadDefCandidates.empty()) {
        for (MachineOperand &MO : MI->operands()) {
          if (!MO.isReg())
            continue;
          Register Reg = MO.getReg();
          if (MO.isDef()) {
            FoldAsLoadDefCandidates.erase(Reg);
            continue;
          }
          if (!FoldAsLoadDefCandidates.count(Reg))
            continue;
          MachineInstr *DefMI = nullptr;
          Register FoldAsLoadDefReg = Reg;
          if (MachineInstr *FoldMI =
                  TII.optimizeLoadInstr(*MI, &MRI, FoldAsLoadDefReg, DefMI)) {
            // Update LocalMIs since we replaced MI with FoldMI and deleted
            // DefMI.
            LLVM_DEBUG(dbgs() << "Replacing: " << *MI);
            LLVM_DEBUG(dbgs() << "     With: " << *FoldMI);
            // Update the call site info.
            if (MI->shouldUpdateAdditionalCallInfo())
              MF.moveAdditionalCallInfo(MI, FoldMI);
            MI->eraseFromParent();
            DefMI->eraseFromParent();
            MRI.markUsesInDebugValueAsUndef(Reg);
            FoldAsLoadDefCandidates.erase(Reg);

            // MI is replaced with FoldMI so we can continue trying to fold
            Changed = true;
            MI = FoldMI;
          }
        }
      }

      if (MI->isLoadFoldBarrier()) {
        LLVM_DEBUG(dbgs() << "Encountered load fold barrier on " << *MI);
        FoldAsLoadDefCandidates.clear();
      }
    }
  }

  struct PtrInfo {
    int64_t Imm;
    MachineInstr *CopyMI;
    MachineInstr *ImmMI;
    Register ImmReg;
    unsigned IncOpc;
  };

  DenseMap<Register, PtrInfo> PtrCache;

  auto canReach = [&](MachineBasicBlock *Start,
                      MachineBasicBlock *Target) -> bool {
    if (Start == Target)
      return true;
    SmallVector<MachineBasicBlock *, 4> Worklist;
    SmallPtrSet<MachineBasicBlock *, 8> Visited;
    Worklist.push_back(Start);
    while (!Worklist.empty()) {
      MachineBasicBlock *BB = Worklist.pop_back_val();
      if (!Visited.insert(BB).second)
        continue;
      for (MachineBasicBlock *Succ : BB->successors()) {
        if (Succ == Target)
          return true;
        Worklist.push_back(Succ);
      }
    }
    return false;
  };

  auto getPtrInfo = [&](Register PtrReg) -> std::optional<PtrInfo> {
    auto It = PtrCache.find(PtrReg);
    if (It != PtrCache.end())
      return It->second;
    if (!PtrReg.isVirtual())
      return std::nullopt;
    if (!MRI.hasOneDef(PtrReg))
      return std::nullopt;
    MachineInstr *PtrDef = MRI.getVRegDef(PtrReg);
    if (!PtrDef)
      return std::nullopt;
    MachineInstr *CopyMI = nullptr;
    MachineInstr *ImmMI = PtrDef;
    if (PtrDef->getOpcode() == TargetOpcode::COPY) {
      if (!PtrDef->getOperand(1).isReg())
        return std::nullopt;
      Register SrcReg = PtrDef->getOperand(1).getReg();
      if (!SrcReg.isVirtual())
        return std::nullopt;
      if (!MRI.hasOneDef(SrcReg))
        return std::nullopt;
      CopyMI = PtrDef;
      ImmMI = MRI.getVRegDef(SrcReg);
      if (!ImmMI)
        return std::nullopt;
    }

    unsigned IncOpc = 0;
    switch (ImmMI->getOpcode()) {
    case Z80::LD16ri:
      IncOpc = Z80::INC16r;
      break;
    case Z80::LD24ri:
      IncOpc = Z80::INC24r;
      break;
    default:
      return std::nullopt;
    }

    int64_t ImmVal = 0;
    bool FoundImm = false;
    for (const MachineOperand &Op : ImmMI->operands()) {
      if (Op.isImm()) {
        ImmVal = Op.getImm();
        FoundImm = true;
        break;
      }
      if (Op.isCImm()) {
        ImmVal = Op.getCImm()->getSExtValue();
        FoundImm = true;
        break;
      }
    }
    if (!FoundImm)
      return std::nullopt;

    Register ImmReg = ImmMI->getOperand(0).isReg()
                          ? ImmMI->getOperand(0).getReg()
                          : Register();
    PtrInfo Info{ImmVal, CopyMI, ImmMI, ImmReg, IncOpc};
    PtrCache.insert({PtrReg, Info});
    return Info;
  };

  struct StoreInfo {
    MachineInstr *MI = nullptr;
    unsigned Opcode = 0;
    Register PtrReg;
    std::optional<PtrInfo> PtrDetail;
    int64_t Addr = 0;
    bool UsesPointer = false;
    bool UsesImmValue = false;
    int64_t ImmValue = 0;
    Register ValReg;
    bool ValIsKill = false;
  };

  auto getStoreInfo = [&](MachineInstr &MI) -> std::optional<StoreInfo> {
    StoreInfo Info;
    Info.MI = &MI;
    Info.Opcode = MI.getOpcode();
    switch (Info.Opcode) {
    case Z80::LD8pg: {
      Info.UsesPointer = true;
      Info.PtrReg = MI.getOperand(0).getReg();
      auto Ptr = getPtrInfo(Info.PtrReg);
      if (!Ptr)
        return std::nullopt;
      Info.PtrDetail = Ptr;
      Info.Addr = Ptr->Imm;
      Info.ValReg = MI.getOperand(1).getReg();
      Info.ValIsKill = MI.getOperand(1).isKill();
      return Info;
    }
    case Z80::LD8pi: {
      Info.UsesPointer = true;
      Info.PtrReg = MI.getOperand(0).getReg();
      auto Ptr = getPtrInfo(Info.PtrReg);
      if (!Ptr)
        return std::nullopt;
      Info.PtrDetail = Ptr;
      Info.Addr = Ptr->Imm;
      Info.UsesImmValue = true;
      Info.ImmValue = MI.getOperand(1).getImm();
      return Info;
    }
    case Z80::LD8ma: {
      if (!MI.getOperand(0).isImm())
        return std::nullopt;
      Info.Addr = MI.getOperand(0).getImm();
      return Info;
    }
    default:
      return std::nullopt;
    }
  };

  auto getDecOpcode = [](unsigned IncOpc) {
    switch (IncOpc) {
    case Z80::INC16r:
      return Z80::DEC16r;
    case Z80::INC24r:
      return Z80::DEC24r;
    default:
      llvm_unreachable("unexpected increment opcode");
    }
  };

  for (MachineBasicBlock &MBB : MF) {
    for (auto I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstr &SecondStore = *I;
      auto NextI = std::next(I);

      std::optional<StoreInfo> SecondInfoOpt = getStoreInfo(SecondStore);
      if (!SecondInfoOpt) {
        I = NextI;
        continue;
      }
      StoreInfo &SecondInfo = *SecondInfoOpt;

      MachineInstr *CopyToA = nullptr;
      Register ValReg;
      bool ValIsKill = false;
      int64_t ValImm = 0;
      MachineBasicBlock::iterator BaseIt;
      if (SecondInfo.Opcode == Z80::LD8ma) {
        // Expect COPY A, <reg> immediately before the absolute store.
        BaseIt = MachineBasicBlock::iterator(SecondStore);
        if (BaseIt == MBB.begin()) {
          I = NextI;
          continue;
        }
        do {
          --BaseIt;
        } while (BaseIt->isDebugInstr() && BaseIt != MBB.begin());
        CopyToA = &*BaseIt;
        if (CopyToA->isDebugInstr() || CopyToA->getOpcode() != TargetOpcode::COPY ||
            CopyToA->getOperand(0).getReg() != Z80::A ||
            !CopyToA->getOperand(1).isReg()) {
          LLVM_DEBUG(dbgs() << "[z80-prera] missing COPY to A before LD8ma\n");
          I = NextI;
          continue;
        }
        ValReg = CopyToA->getOperand(1).getReg();
        ValIsKill = CopyToA->getOperand(1).isKill();

        if (BaseIt == MBB.begin()) {
          I = NextI;
          continue;
        }
        do {
          --BaseIt;
        } while (BaseIt->isDebugInstr() && BaseIt != MBB.begin());
      } else {
        // Base store should be immediately before the second store.
        BaseIt = MachineBasicBlock::iterator(SecondStore);
        if (BaseIt == MBB.begin()) {
          I = NextI;
          continue;
        }
        do {
          --BaseIt;
        } while (BaseIt->isDebugInstr() && BaseIt != MBB.begin());

        if (SecondInfo.UsesImmValue) {
          ValImm = SecondInfo.ImmValue;
        } else if (SecondInfo.UsesPointer && SecondInfo.Opcode == Z80::LD8pg) {
          ValReg = SecondStore.getOperand(1).getReg();
          ValIsKill = SecondStore.getOperand(1).isKill();
        }
      }

      MachineInstr &BaseStore = *BaseIt;
      std::optional<StoreInfo> BaseInfoOpt = getStoreInfo(BaseStore);
      if (!BaseInfoOpt || !BaseInfoOpt->UsesPointer) {
        I = NextI;
        continue;
      }
      StoreInfo &BaseInfo = *BaseInfoOpt;

      if (!BaseInfo.PtrDetail) {
        I = NextI;
        continue;
      }
      int64_t BaseAddr = BaseInfo.Addr;
      int64_t SecondAddr = SecondInfo.Addr;
      int64_t Delta = SecondAddr - BaseAddr;
      if (Delta != 1 && Delta != -1) {
        I = NextI;
        continue;
      }

      Register PtrBase = BaseInfo.PtrReg;
      std::optional<PtrInfo> SecondPtrInfo = SecondInfo.PtrDetail;

      // Determine if the pointer value is needed later in the block.
      bool PtrUsedLater = false;
      for (auto ScanI = NextI; ScanI != E && !PtrUsedLater; ++ScanI) {
        if (ScanI->isDebugInstr())
          continue;
        for (const MachineOperand &MO : ScanI->operands())
          if (MO.isReg() && MO.getReg() == PtrBase) {
            PtrUsedLater = true;
            break;
          }
      }
      if (!PtrUsedLater)
        for (MachineInstr &UseMI : MRI.use_instructions(PtrBase)) {
          if (&UseMI == &BaseStore || &UseMI == &SecondStore)
            continue;
          MachineBasicBlock *UseBB = UseMI.getParent();
          if (UseBB != &MBB && canReach(&MBB, UseBB)) {
            PtrUsedLater = true;
            break;
          }
        }

      // Set up pointer step and optional restore.
      unsigned IncOpc = BaseInfo.PtrDetail->IncOpc;
      unsigned StepOpc = Delta > 0 ? IncOpc : getDecOpcode(IncOpc);
      unsigned RestoreOpc = Delta > 0 ? getDecOpcode(IncOpc) : IncOpc;

      BaseStore.getOperand(0).setIsKill(false);

      // Insert INC/DEC before the second store.
      BuildMI(MBB, MachineBasicBlock::iterator(SecondStore), SecondStore.getDebugLoc(),
              TII.get(StepOpc), PtrBase)
          .addReg(PtrBase, RegState::Kill);

      LLVM_DEBUG(dbgs() << "[z80-prera] Rewriting store pair: base=" << BaseAddr
                        << " second=" << SecondAddr << " delta=" << Delta << "\n");

      if (CopyToA) {
        bool AUsedLater = false;
        auto ScanI = std::next(MachineBasicBlock::iterator(SecondStore));
        for (; ScanI != MBB.end(); ++ScanI) {
          if (ScanI->isDebugInstr())
            continue;
          if (ScanI->definesRegister(Z80::A, &TRI))
            break;
          if (ScanI->readsRegister(Z80::A, &TRI)) {
            AUsedLater = true;
            break;
          }
        }

        if (!AUsedLater && ScanI == MBB.end()) {
          LiveRegUnits LiveOutUnits(TRI);
          LiveOutUnits.addLiveOuts(MBB);
          if (!LiveOutUnits.available(Z80::A))
            AUsedLater = true;
        }

        if (AUsedLater) {
          I = NextI;
          continue;
        }
      }

      MachineInstrBuilder NewStore;
      unsigned PtrFlags = PtrUsedLater ? 0 : RegState::Kill;
      switch (SecondInfo.Opcode) {
      case Z80::LD8ma: {
        NewStore = BuildMI(MBB, MachineBasicBlock::iterator(SecondStore),
                           SecondStore.getDebugLoc(), TII.get(Z80::LD8pg))
                       .addReg(PtrBase, PtrFlags);
        NewStore.addReg(ValReg, ValIsKill ? RegState::Kill : 0);
        break;
      }
      case Z80::LD8pi: {
        NewStore = BuildMI(MBB, MachineBasicBlock::iterator(SecondStore),
                           SecondStore.getDebugLoc(), TII.get(Z80::LD8pi))
                       .addReg(PtrBase, PtrFlags)
                       .addImm(ValImm);
        break;
      }
      case Z80::LD8pg: {
        Register Src = SecondStore.getOperand(1).getReg();
        bool SrcKill = SecondStore.getOperand(1).isKill();
        NewStore = BuildMI(MBB, MachineBasicBlock::iterator(SecondStore),
                           SecondStore.getDebugLoc(), TII.get(Z80::LD8pg))
                       .addReg(PtrBase, PtrFlags)
                       .addReg(Src, SrcKill ? RegState::Kill : 0);
        break;
      }
      default:
        llvm_unreachable("Unexpected store opcode");
      }
      NewStore.cloneMemRefs(SecondStore);

      // Remove the original second store (and the COPY if present).
      SecondStore.eraseFromParent();
      if (CopyToA)
        CopyToA->eraseFromParent();

      // Drop the alternate pointer if it is now unused.
      if (SecondInfo.UsesPointer && SecondInfo.PtrReg && SecondInfo.PtrReg != PtrBase &&
          MRI.use_empty(SecondInfo.PtrReg)) {
        if (SecondPtrInfo) {
          if (SecondPtrInfo->CopyMI)
            SecondPtrInfo->CopyMI->eraseFromParent();
          if (SecondPtrInfo->ImmMI && MRI.use_empty(SecondPtrInfo->ImmMI->getOperand(0).getReg()))
            SecondPtrInfo->ImmMI->eraseFromParent();
        }
      }

      if (PtrUsedLater) {
        MachineInstr *StoreMI = NewStore.getInstr();
        BuildMI(MBB, std::next(MachineBasicBlock::iterator(StoreMI)),
                StoreMI->getDebugLoc(), TII.get(RestoreOpc), PtrBase)
            .addReg(PtrBase);
      }

      Changed = true;
      I = NextI;
      continue;
    }
  }

  Changed |= separateStableLanes(MF, Z80::sub_high);
  Changed |= separateStableLanes(MF, Z80::sub_low);
  return selectBankInstructions(MF) || Changed;
}

char Z80MachinePreRAOptimization::ID = 0;
INITIALIZE_PASS_BEGIN(Z80MachinePreRAOptimization, DEBUG_TYPE,
                      "Optimize Z80 machine instrs before regalloc", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_END(Z80MachinePreRAOptimization, DEBUG_TYPE,
                    "Optimize Z80 machine instrs before regalloc", false, false)

FunctionPass *llvm::createZ80MachinePreRAOptimizationPass() {
  return new Z80MachinePreRAOptimization();
}

char Z80PrepareLiveRanges::ID = 0;
INITIALIZE_PASS_BEGIN(Z80PrepareLiveRanges, "z80-prepare-live-ranges",
                      "Prepare Z80 live ranges for allocation", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_END(Z80PrepareLiveRanges, "z80-prepare-live-ranges",
                    "Prepare Z80 live ranges for allocation", false, false)
FunctionPass *llvm::createZ80PrepareLiveRangesPass() {
  return new Z80PrepareLiveRanges();
}
