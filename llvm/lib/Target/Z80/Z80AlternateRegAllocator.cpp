//===- Z80AlternateRegAllocator.cpp - Allocate bank storage
//----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Allocate spilled values to alternate-bank registers. A bank is a storage
// resource, not another set of directly addressable instruction operands.
// Transfers initially restore the primary bank after every access; the late
// region allocator can retain it across arithmetic. Liveness includes definitions
// (even dead stores),
// backedges and call clobbers. Nonoverlapping spill slots can share storage.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/InitializePasses.h"

using namespace llvm;
#define DEBUG_TYPE "z80-alternate-regalloc"
STATISTIC(NumBankSlots, "Spill slots allocated to the Z80 alternate bank");

namespace {
struct Access {
  MachineInstr *MI;
  unsigned Slot;
  MCRegister Reg;
  bool Load;
};
struct Slot {
  unsigned Width = 0;
  bool Valid = true;
  double Saving = 0, PairByteSaving = 0;
  MCRegister Assigned;
  unsigned UnavailableBanks = 0;
};

static unsigned bankMask(MCRegister R) {
  switch (R.id()) {
  case Z80::A_ALT:
    return 1;
  case Z80::L_ALT:
    return 2;
  case Z80::H_ALT:
    return 4;
  case Z80::E_ALT:
    return 8;
  case Z80::D_ALT:
    return 16;
  case Z80::C_ALT:
    return 32;
  case Z80::B_ALT:
    return 64;
  case Z80::HL_ALT:
    return 2 | 4;
  case Z80::DE_ALT:
    return 8 | 16;
  case Z80::BC_ALT:
    return 32 | 64;
  case Z80::UHL_ALT:
    return 2 | 4 | 128;
  case Z80::UDE_ALT:
    return 8 | 16 | 256;
  case Z80::UBC_ALT:
    return 32 | 64 | 512;
  default:
    llvm_unreachable("Unexpected alternate register");
  }
}

// Native stack-relative access versus a bank transfer. These reflect actual
// pseudo expansion, including saving the accumulator or an index-load scratch.
static int saving(const Access &A, unsigned Width, bool Bytes,
                  bool PairByte = false) {
  bool Index = Z80::I8RegClass.contains(A.Reg) ||
               Z80::I16RegClass.contains(A.Reg) ||
               Z80::I24RegClass.contains(A.Reg);
  if (Width > 1) {
    unsigned Old = Bytes ? (Width == 2 ? 6 : 3) : 38;
    if (Width == 2 && Index)
      Old += Bytes ? (A.Load ? 4 : 6) : (A.Load ? 44 : 65);
    unsigned New = Bytes ? 4 + unsigned(Index) : 29 + 4 * unsigned(Index);
    return int(Old) - int(New);
  }
  unsigned Old = Bytes ? (Index ? 7 : 3) : (Index ? 48 : 19);
  if (PairByte) {
    unsigned New = A.Reg == Z80::A ? (Bytes ? 3 : 12)
                                 : (Bytes ? 6 + unsigned(Index)
                                          : 37 + 4 * unsigned(Index));
    return int(Old) - int(New);
  }
  unsigned New = A.Reg == Z80::A
                     ? (Bytes ? (A.Load ? 6 : 4) : (A.Load ? 37 : 29))
                     : (Bytes ? 3 + unsigned(Index) : 12 + 4 * unsigned(Index));
  return int(Old) - int(New);
}

class Z80AlternateRegAllocator final : public MachineFunctionPass {
public:
  static char ID;
  Z80AlternateRegAllocator() : MachineFunctionPass(ID) {
    initializeZ80AlternateRegAllocatorPass(*PassRegistry::getPassRegistry());
  }
  StringRef getPassName() const override {
    return "Z80 Alternate Bank Allocator";
  }
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreserved<LiveStacksWrapperLegacy>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace

bool Z80AlternateRegAllocator::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<Z80Subtarget>();
  // An interrupt handler may already be storing the interrupted main bank in
  // this bank. Preserve-all functions likewise must not acquire new clobbers.
  if (!ST.useAlternateRegs() || MF.getFunction().hasFnAttribute("interrupt") ||
      MF.getFunction().hasFnAttribute(Attribute::Naked) ||
      MF.getFunction().getCallingConv() == CallingConv::PreserveAll)
    return false;
  auto &MFI = MF.getFrameInfo();
  unsigned N = MFI.getObjectIndexEnd();
  if (!N || N > 256)
    return false;
  auto &MBFI = getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  const auto &TII = *ST.getInstrInfo();
  SmallVector<Slot, 16> Slots(N);
  SmallVector<Access, 32> Accesses;
  DenseMap<const MachineInstr *, unsigned> AccessIndex;
  bool Bytes = MF.getFunction().hasOptSize() || ST.hasEZ80Ops();
  for (unsigned I = 0; I != N; ++I) {
    Slots[I].Valid = MFI.isSpillSlotObjectIndex(I) &&
                     !MFI.isDeadObjectIndex(I) && MFI.getStackID(I) == 0;
    if (Slots[I].Valid)
      Slots[I].Width = MFI.getObjectSize(I);
    Slots[I].Valid &=
        Slots[I].Width == 1 || Slots[I].Width == (ST.is24Bit() ? 3 : 2);
  }
  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      // An empty compiler memory barrier emits no instructions and has no
      // register effects. Keep bank storage across it; region scheduling still
      // treats it as a boundary. Other assembly can access bank contents outside
      // its declared operands, so it remains a function-wide exclusion.
      bool MemoryBarrier =
          MI.getOpcode() == TargetOpcode::INLINEASM &&
          MI.getOperand(0).getSymbolName()[0] == '\0' &&
          !(MI.getOperand(1).getImm() &
            ~(InlineAsm::Extra_HasSideEffects | InlineAsm::Extra_MayLoad |
              InlineAsm::Extra_MayStore)) &&
          llvm::all_of(llvm::drop_begin(MI.operands(), 2),
                       [](const MachineOperand &MO) { return MO.isMetadata(); });
      if ((MI.isInlineAsm() && !MemoryBarrier) || MI.getOpcode() == Z80::EXAF ||
          MI.getOpcode() == Z80::EXX16 || MI.getOpcode() == Z80::EXX24)
        return false;
      int FI = -1;
      unsigned Count = 0;
      for (const auto &MO : MI.operands())
        if (MO.isFI() && MO.getIndex() >= 0) {
          FI = MO.getIndex();
          ++Count;
        }
      if (FI < 0 || unsigned(FI) >= N)
        continue;
      if (Count != 1) {
        for (const auto &MO : MI.operands())
          if (MO.isFI() && MO.getIndex() >= 0 && unsigned(MO.getIndex()) < N)
            Slots[MO.getIndex()].Valid = false;
        continue;
      }
      auto &S = Slots[FI];
      if (!S.Valid)
        continue;
      // Target-created spill pseudos can lack MMOs. Their private frame object
      // and the complete access scan establish that they are ordinary storage.
      bool Load = false,
           Eligible = !MI.isDebugInstr() &&
                      (MI.memoperands_empty() || !MI.hasOrderedMemoryRef());
      switch (MI.getOpcode()) {
      case Z80::LD8ro:
      case Z80::LD8go:
      case Z80::LD88ro:
      case Z80::LD16ro:
      case Z80::LD24ro:
        Load = true;
        break;
      case Z80::LD8or:
      case Z80::LD8og:
      case Z80::LD88or:
      case Z80::LD16or:
      case Z80::LD24or:
        break;
      default:
        Eligible = false;
        break;
      }
      unsigned RegOp = Load ? 0 : MI.getNumExplicitOperands() - 1;
      unsigned OffsetOp = Load ? 2 : 1;
      Eligible &= MI.getNumExplicitOperands() > OffsetOp &&
                  MI.getOperand(OffsetOp).isImm() &&
                  MI.getOperand(OffsetOp).getImm() == 0 &&
                  MI.getOperand(RegOp).isReg();
      if (!Eligible) {
        S.Valid = false;
        continue;
      }
      MCRegister R = MI.getOperand(RegOp).getReg().asMCReg();
      bool RightWidth = S.Width == 1   ? Z80::R8RegClass.contains(R)
                        : S.Width == 2 ? Z80::R16RegClass.contains(R)
                                       : Z80::R24RegClass.contains(R);
      if (!RightWidth) {
        S.Valid = false;
        continue;
      }
      AccessIndex[&MI] = Accesses.size();
      Accesses.push_back({&MI, unsigned(FI), R, Load});
      double Weight = MF.getFunction().hasOptSize()
                          ? 1.0
                          : MBFI.getBlockFreqRelativeToEntryBlock(&MBB);
      S.Saving += Weight * saving(Accesses.back(), S.Width, Bytes);
      if (S.Width == 1)
        S.PairByteSaving += Weight * saving(Accesses.back(), 1, Bytes, true);
    }

  struct CursorRegion {
    unsigned Slot;
    MCRegister Pair, Byte;
    bool LoadByte;
    double Weight;
  };
  DenseMap<const MachineInstr *, CursorRegion> CursorRegions;
  if (ST.is16Bit() && MF.getProperties().hasTracksLiveness())
    for (const auto &A : Accesses) {
      if (!A.Load || !Slots[A.Slot].Valid || Slots[A.Slot].Width != 2 ||
          !Z80::G16RegClass.contains(A.Reg) || A.MI->isBundled())
        continue;
      auto I = std::next(A.MI->getIterator());
      auto End = A.MI->getParent()->end();
      if (I == End)
        continue;
      auto &Memory = *I++;
      bool Read = Memory.getOpcode() == Z80::LD8ap ||
                  Memory.getOpcode() == Z80::LD8gp;
      bool Write = Memory.getOpcode() == Z80::LD8pa ||
                   Memory.getOpcode() == Z80::LD8pg;
      if ((!Read && !Write) || Memory.isBundled() ||
          Memory.getOperand(Read ? 1 : 0).getReg() != A.Reg || I == End)
        continue;
      MCRegister Byte = Memory.getOperand(Read ? 0 : 1).getReg();
      const auto &TRI = *ST.getRegisterInfo();
      if (TRI.regsOverlap(Byte, A.Reg) ||
          (Byte != Z80::A && !Z80::G8RegClass.contains(Byte)))
        continue;
      auto &Step = *I++;
      if ((Step.getOpcode() != Z80::INC16r &&
           Step.getOpcode() != Z80::DEC16r) || Step.isBundled() ||
          Step.getOperand(0).getReg() != A.Reg ||
          Step.getOperand(1).getReg() != A.Reg || I == End)
        continue;
      auto Found = AccessIndex.find(&*I);
      if (Found == AccessIndex.end())
        continue;
      const auto &Save = Accesses[Found->second];
      if (Save.Load || Save.Slot != A.Slot || Save.Reg != A.Reg ||
          Save.MI->isBundled())
        continue;
      CursorRegions.try_emplace(
          &*I, CursorRegion{A.Slot, A.Reg, Byte, Read,
                            MF.getFunction().hasOptSize()
                                ? 1.0
                                : MBFI.getBlockFreqRelativeToEntryBlock(
                                      A.MI->getParent())});
    }

  unsigned Blocks = MF.getNumBlockIDs();
  SmallVector<BitVector, 16> In(Blocks, BitVector(N)), Out(In);
  bool Changed = true;
  unsigned Iterations = 0;
  while (Changed && ++Iterations <= 256) {
    Changed = false;
    for (auto &MBB : reverse(MF)) {
      BitVector Live(N);
      for (const auto *Succ : MBB.successors())
        Live |= In[Succ->getNumber()];
      Out[MBB.getNumber()] = Live;
      for (const auto &MI : reverse(MBB)) {
        auto It = AccessIndex.find(&MI);
        if (It == AccessIndex.end())
          continue;
        const auto &A = Accesses[It->second];
        if (A.Load)
          Live.set(A.Slot);
        else
          Live.reset(A.Slot);
      }
      if (Live != In[MBB.getNumber()]) {
        In[MBB.getNumber()] = std::move(Live);
        Changed = true;
      }
    }
  }
  if (Changed)
    return false;

  SmallVector<BitVector, 16> Conflicts(N, BitVector(N));
  const auto &TRI = *ST.getRegisterInfo();
  SmallVector<MCRegister, 7> Banks = {Z80::A_ALT};
  if (ST.is24Bit())
    Banks.append({Z80::UHL_ALT, Z80::UDE_ALT, Z80::UBC_ALT});
  else
    Banks.append({Z80::L_ALT, Z80::H_ALT, Z80::E_ALT, Z80::D_ALT,
                  Z80::C_ALT, Z80::B_ALT});
  bool HasFixedBanks = llvm::any_of(Banks, [&](MCRegister R) {
    return MF.getRegInfo().isPhysRegUsed(R, true);
  });
  for (const auto &MBB : MF) {
    BitVector Live = Out[MBB.getNumber()];
    /* Fixed assignments from the main allocator only exclude storage while
       they are live. Include reads and dead definitions at an access itself:
       a dead bank write still destroys a spill stored there. Call masks enter
       the same conflict calculation; unknown calls clobber every bank. */
    LiveRegUnits FixedLive(TRI);
    bool TrackRegisters = HasFixedBanks || !CursorRegions.empty();
    if (TrackRegisters)
      FixedLive.addLiveOuts(MBB);
    for (const auto &MI : reverse(MBB)) {
      bool HasMask = llvm::any_of(MI.operands(), [](const MachineOperand &MO) {
        return MO.isRegMask();
      });
      unsigned FixedBanks = 0;
      if (HasFixedBanks || HasMask)
        for (MCRegister R : Banks)
          if ((HasFixedBanks && !FixedLive.available(R)) ||
              MI.modifiesRegister(R, &TRI) || MI.readsRegister(R, &TRI))
            FixedBanks |= bankMask(R);
      if (MI.isCall() && !HasMask)
        FixedBanks = ~0u;
      auto Region = CursorRegions.find(&MI);
      if (Region != CursorRegions.end()) {
        const auto &R = Region->second;
        bool Bridge = R.Byte != Z80::A;
        bool ExportByte = Bridge && R.LoadByte && !FixedLive.available(R.Byte);
        bool PreserveA = Bridge && !FixedLive.available(Z80::A);
        bool ExportPair = !FixedLive.available(R.Pair);
        /* Price the entire cursor interval: its bank activation replaces both
           transfers. An independent byte crosses through A, which may need
           preservation. Budget a stack save when AF' availability is unknown.
           This keeps equivalent spill traffic from hiding different costs
           for input and output cursors. */
        unsigned Cost = (Bytes ? 2 : 8) +
                        (unsigned(Bridge && !R.LoadByte) + ExportByte) *
                            (Bytes ? 1 : 4) +
                        PreserveA * (Bytes ? 2 : 21) +
                        ExportPair * (Bytes ? 4 : 29);
        unsigned Transfers = Bytes ? 8 : 58;
        if (Cost < Transfers)
          Slots[R.Slot].Saving += R.Weight * (Transfers - Cost);
      }
      if (TrackRegisters)
        FixedLive.stepBackward(MI);
      for (int I : Live.set_bits())
        Slots[I].UnavailableBanks |= FixedBanks;
      auto It = AccessIndex.find(&MI);
      if (It == AccessIndex.end())
        continue;
      const auto &A = Accesses[It->second];
      Slots[A.Slot].UnavailableBanks |= FixedBanks;
      // Include dead definitions: a store still overwrites the physical bank.
      Conflicts[A.Slot] |= Live;
      if (A.Load)
        Live.set(A.Slot);
      else
        Live.reset(A.Slot);
    }
  }
  for (unsigned I = 0; I != N; ++I) {
    Conflicts[I].reset(I);
    for (int J : Conflicts[I].set_bits())
      Conflicts[J].set(I);
  }
  auto BestSaving = [&](unsigned I) {
    return Slots[I].Width == 1
               ? std::max(Slots[I].Saving, Slots[I].PairByteSaving)
               : Slots[I].Saving;
  };
  SmallVector<unsigned, 16> Order;
  for (unsigned I = 0; I != N; ++I)
    if (Slots[I].Valid && BestSaving(I) > 0)
      Order.push_back(I);
  llvm::stable_sort(Order, [&](unsigned A, unsigned B) {
    return BestSaving(A) > BestSaving(B);
  });
  for (unsigned I : Order) {
    const auto &RC = Slots[I].Width == 1   ? Z80::ALT8RegClass
                     : Slots[I].Width == 2 ? Z80::ALT16RegClass
                                           : Z80::ALT24RegClass;
    double Best = 0;
    for (MCRegister R : RC) {
      double Saving = Slots[I].Width == 1 && R != Z80::A_ALT
                          ? Slots[I].PairByteSaving
                          : Slots[I].Saving;
      if (Saving <= Best || (Slots[I].UnavailableBanks & bankMask(R)))
        continue;
      bool Available = true;
      for (int J : Conflicts[I].set_bits())
        if (Slots[J].Assigned && TRI.regsOverlap(Slots[J].Assigned, R)) {
          Available = false;
          break;
        }
      if (Available) {
        Slots[I].Assigned = R;
        Best = Saving;
      }
    }
  }
  bool Any = false;
  auto *Indexes = getAnalysisIfAvailable<SlotIndexesWrapperPass>();
  for (const auto &A : Accesses) {
    auto &S = Slots[A.Slot];
    if (!S.Assigned)
      continue;
    unsigned Opc = S.Width == 1 ? (A.Load ? Z80::ALT_LOAD8 : Z80::ALT_SAVE8)
                   : S.Width == 2
                       ? (A.Load ? Z80::ALT_LOAD16 : Z80::ALT_SAVE16)
                       : (A.Load ? Z80::ALT_LOAD24 : Z80::ALT_SAVE24);
    auto Replacement =
        BuildMI(*A.MI->getParent(), A.MI, A.MI->getDebugLoc(), TII.get(Opc),
                A.Load ? A.Reg : S.Assigned)
            .addReg(A.Load ? S.Assigned : A.Reg);
    /* The remaining stack intervals keep their original instruction positions.
       Coloring ignores the removed frame objects and can still merge the
       unassigned spills using the intervals recorded during allocation. */
    if (Indexes)
      Indexes->getSI().replaceMachineInstrInMaps(*A.MI, *Replacement);
    A.MI->eraseFromParent();
    Any = true;
  }
  for (auto &MBB : MF) {
    for (int I : In[MBB.getNumber()].set_bits())
      if (Slots[I].Assigned)
        MBB.addLiveIn(Slots[I].Assigned);
    MBB.sortUniqueLiveIns();
  }
  for (unsigned I : Order)
    if (Slots[I].Assigned) {
      MFI.RemoveStackObject(I);
      ++NumBankSlots;
    }
  return Any;
}

char Z80AlternateRegAllocator::ID = 0;
INITIALIZE_PASS_BEGIN(Z80AlternateRegAllocator, DEBUG_TYPE,
                      "Z80 Alternate Bank Allocator", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(Z80AlternateRegAllocator, DEBUG_TYPE,
                    "Z80 Alternate Bank Allocator", false, false)
FunctionPass *llvm::createZ80AlternateRegAllocator() {
  return new Z80AlternateRegAllocator();
}
