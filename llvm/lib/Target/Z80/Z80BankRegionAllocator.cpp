//===- Z80BankRegionAllocator.cpp - Allocate switched regions
//--------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Keep a bank-resident value in its bank across a straight-line arithmetic
// region. Allocate the region's operands to the active bank and unbanked
// registers; reconcile live results on exit. Run after scheduling and pseudo
// expansion, before branch relaxation, so nothing can move across EXX.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80MachineFunctionInfo.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCDwarf.h"

using namespace llvm;
#define DEBUG_TYPE "z80-bank-regions"
STATISTIC(NumBankRegions, "Arithmetic regions allocated to the alternate bank");
STATISTIC(NumBankJoins, "Adjacent alternate-bank arithmetic regions joined");

namespace {
MCRegister mainReg(MCRegister Alt) {
  switch (Alt.id()) {
  case Z80::B_ALT:
    return Z80::B;
  case Z80::C_ALT:
    return Z80::C;
  case Z80::D_ALT:
    return Z80::D;
  case Z80::E_ALT:
    return Z80::E;
  case Z80::H_ALT:
    return Z80::H;
  case Z80::L_ALT:
    return Z80::L;
  case Z80::BC_ALT:
    return Z80::BC;
  case Z80::DE_ALT:
    return Z80::DE;
  case Z80::HL_ALT:
    return Z80::HL;
  case Z80::UBC_ALT:
    return Z80::UBC;
  case Z80::UDE_ALT:
    return Z80::UDE;
  case Z80::UHL_ALT:
    return Z80::UHL;
  default:
    return MCRegister();
  }
}

bool switched(MCRegister R, const TargetRegisterInfo &TRI) {
  return TRI.regsOverlap(R, Z80::UBC) || TRI.regsOverlap(R, Z80::UDE) ||
         TRI.regsOverlap(R, Z80::UHL);
}

class Z80BankRegionAllocator final : public MachineFunctionPass {
public:
  static char ID;
  Z80BankRegionAllocator() : MachineFunctionPass(ID) {
    initializeZ80BankRegionAllocatorPass(*PassRegistry::getPassRegistry());
  }
  StringRef getPassName() const override { return "Z80 Bank Region Allocator"; }
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

bool Z80BankRegionAllocator::runOnMachineFunction(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<Z80Subtarget>();
  if (!ST.useAlternateRegs() || !MF.getProperties().hasTracksLiveness() ||
      MF.getFunction().hasFnAttribute("interrupt") ||
      MF.getFunction().hasFnAttribute(Attribute::Naked) ||
      MF.getFunction().getCallingConv() == CallingConv::PreserveAll)
    return false;
  const auto &TII = *ST.getInstrInfo();
  const auto &TRI = *ST.getRegisterInfo();
  bool Wide = ST.is24Bit(),
       Bytes = ST.hasEZ80Ops() || MF.getFunction().hasOptSize();
  unsigned Load = Wide ? Z80::ALT_LOAD24 : Z80::ALT_LOAD16;
  unsigned Save = Wide ? Z80::ALT_SAVE24 : Z80::ALT_SAVE16;
  // If AF' has no storage users, it can hold A while the other bank executes
  // a region that neither reads nor writes flags. Calls are region boundaries.
  bool FreeAF = !MF.getRegInfo().isPhysRegUsed(Z80::AF_ALT, true);
  bool Changed = false;
  SmallPtrSet<MachineInstr *, 8> Entries, Exits;
  // Adjacent operations already allocated to the alternate bank share one
  // activation. Their operands express residency before this lowering step;
  // the late decision here only removes redundant exchanges.
  auto BankOpcode = [&](const MachineInstr &MI) -> unsigned {
    unsigned Opc = MI.getOpcode(), Op = Opc == Z80::BANK_LD8ap ? 1 : 0;
    unsigned MainOpc = 0;
    switch (Opc) {
    case Z80::BANK_BIT8pb:
      MainOpc = Z80::BIT8pb;
      break;
    case Z80::BANK_LD8pi:
      MainOpc = Z80::LD8pi;
      break;
    case Z80::ALT_SAVE8:
    case Z80::ALT_LOAD8:
      Op = Opc == Z80::ALT_LOAD8 ? 1 : 0;
      if (MI.getOperand(1 - Op).getReg() != Z80::A)
        return 0;
      MainOpc = Z80::LD8gg;
      break;
    case Z80::BANK_ADD16:
      if (!mainReg(MI.getOperand(2).getReg()))
        return 0;
      MainOpc = Z80::ADD16rr;
      break;
    case Z80::BANK_LD16ri:
      MainOpc = Z80::LD16ri;
      break;
    case Z80::BANK_LD16rm:
      MainOpc = mainReg(MI.getOperand(0).getReg()) == Z80::HL
                    ? Z80::LD16am : Z80::LD16gm;
      break;
    case Z80::BANK_LD16mr:
      Op = 1;
      MainOpc = mainReg(MI.getOperand(1).getReg()) == Z80::HL
                    ? Z80::LD16ma : Z80::LD16mg;
      break;
    case Z80::BANK_INCL16:
      MainOpc = Z80::INC8r;
      break;
    case Z80::BANK_DECL16:
      MainOpc = Z80::DEC8r;
      break;
    case Z80::BANK_INC16:
      MainOpc = Z80::INC16r;
      break;
    case Z80::BANK_DEC16:
      MainOpc = Z80::DEC16r;
      break;
    case Z80::BANK_LD8ap:
      MainOpc = Z80::LD8ap;
      break;
    case Z80::BANK_LD8pa:
      MainOpc = Z80::LD8pa;
      break;
    }
    return MainOpc && mainReg(MI.getOperand(Op).getReg()) ? MainOpc : 0;
  };

  /* A leaf whose body uses only resident pairs and unbanked operands can
     keep one activation through branches and loop backedges. Verify the
     physical assignment before applying the allocation-time cost estimate. */
  if (!Wide && MF.getInfo<Z80MachineFunctionInfo>()->hasSingleBankRegion()) {
    bool Legal = true, HasBank = false;
    for (const auto &MBB : MF)
      for (const auto &MI : MBB) {
        if (BankOpcode(MI)) {
          HasBank = true;
          continue;
        }
        if (MI.isCall() || MI.isInlineAsm() || MI.isBundled() ||
            (MI.hasUnmodeledSideEffects() && !MI.isReturn())) {
          Legal = false;
          break;
        }
        if (MI.isReturn())
          continue;
        for (const auto &MO : MI.operands())
          if (MO.isRegMask() ||
              (MO.isReg() && MO.getReg() &&
               (switched(MO.getReg(), TRI) || mainReg(MO.getReg()))))
            Legal = false;
      }
    if (Legal && HasBank) {
      SmallVector<MachineBasicBlock *, 8> Blocks;
      for (auto &MBB : MF) {
        Blocks.push_back(&MBB);
        for (auto &MI : MBB) {
          unsigned Opc = BankOpcode(MI);
          if (!Opc)
            continue;
          bool Low = MI.getOpcode() == Z80::BANK_INCL16 ||
                     MI.getOpcode() == Z80::BANK_DECL16;
          MI.setDesc(TII.get(Opc));
          for (auto &MO : MI.operands())
            if (MO.isReg() && MO.getReg()) {
              if (MCRegister R = mainReg(MO.getReg()))
                MO.setReg(Low ? TRI.getSubReg(R, Z80::sub_low) : R);
              if (MO.isDef())
                MO.setIsDead(false);
              else
                MO.setIsKill(false);
            }
        }
        for (auto &MI : MBB)
          if (MI.isReturn()) {
            auto Exit = BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(Z80::EXX16));
            for (auto &MO : Exit->operands())
              if (MO.isReg() && MO.isUse())
                MO.setIsUndef(true);
            for (const auto &MO : MI.operands())
              if (MO.isReg() && MO.isUse())
                if (MCRegister R = mainReg(MO.getReg()))
                  Exit.addReg(R, RegState::Implicit);
          }
      }
      auto Enter = BuildMI(MF.front(), MF.front().begin(), DebugLoc(),
                           TII.get(Z80::EXX16));
      for (auto &MO : Enter->operands())
        if (MO.isReg() && MO.isUse())
          MO.setIsUndef(true);
      for (auto LI : MF.front().liveins())
        Enter.addReg(LI.PhysReg, RegState::Implicit);
      for (auto *MBB : Blocks)
        MBB->clearLiveIns();
      fullyRecomputeLiveIns(Blocks);
      ++NumBankRegions;
      return true;
    }
  }

  for (auto &MBB : MF) {
    if (llvm::none_of(MBB, [=](const MachineInstr &MI) {
          return MI.getOpcode() == Load;
        }))
      continue;
    // Only materialize liveness at potential region exits. No CFG iteration or
    // interval graph is needed; work is linear plus a bounded scan per entry.
    DenseMap<const MachineInstr *, BitVector> LiveAfter;
    LiveRegUnits Live(TRI);
    Live.addLiveOuts(MBB);
    for (const auto &MI : reverse(MBB)) {
      if (MI.getOpcode() == Save ||
          ((MI.getOpcode() == Z80::LD8ap || MI.getOpcode() == Z80::LD8gp) &&
           MI.getIterator() != MBB.begin() &&
           std::prev(MI.getIterator())->getOpcode() == Load))
        LiveAfter.try_emplace(&MI, Live.getBitVector());
      Live.stepBackward(MI);
    }
    for (auto It = MBB.begin(); It != MBB.end();) {
      auto &Entry = *It++;
      if (Entry.getOpcode() != Load)
        continue;
      MCRegister Main = Entry.getOperand(0).getReg();
      MCRegister Alt = Entry.getOperand(1).getReg();
      MCRegister Bank = mainReg(Alt);
      if (!Bank || (!switched(Main, TRI) &&
                    !Z80::I16RegClass.contains(Main) &&
                    !Z80::I24RegClass.contains(Main)))
        continue;
      // A read-only cursor does not need a matching save. Read through its
      // resident bank when the temporary main-bank pointer dies at the load.
      // This also avoids moving an alternate pair through an index register
      // merely to issue a single byte load.
      if (It != MBB.end() &&
          (It->getOpcode() == Z80::LD8ap || It->getOpcode() == Z80::LD8gp) &&
          It->getOperand(1).getReg() == Main) {
        auto &Read = *It;
        LiveRegUnits Out(TRI);
        Out.addUnits(LiveAfter.find(&Read)->second);
        MCRegister Byte = Read.getOperand(0).getReg();
        bool Bridge = Byte != Z80::A;
        bool PreserveA = Bridge && !Out.available(Z80::A);
        /* A load may overwrite a lane of its own address. That result is
           exported through A after EXX; only the remaining address lanes
           must be dead. A live accumulator can cross through unused AF'. */
        Out.removeReg(Byte);
        unsigned Cost = (Bytes ? 2 : 8) + Bridge * (Bytes ? 1 : 4) +
                        PreserveA * (Bytes ? 2 : 8);
        if (Z80::G8RegClass.contains(Byte) && Out.available(Main) &&
            (!PreserveA || FreeAF) && Cost < (Bytes ? 4u : 29u)) {
          if (PreserveA)
            BuildMI(MBB, Entry, Entry.getDebugLoc(), TII.get(Z80::EXAF));
          auto Enter = BuildMI(MBB, Entry, Entry.getDebugLoc(),
                               TII.get(Wide ? Z80::EXX24 : Z80::EXX16));
          Entries.insert(Enter);
          for (auto &MO : Enter->operands())
            if (MO.isReg() && MO.isUse() &&
                (switched(MO.getReg(), TRI) ||
                 (mainReg(MO.getReg()) && MO.getReg() != Alt &&
                  Out.available(MO.getReg()))))
              MO.setIsUndef(true);
          for (MCRegister R : {Z80::B, Z80::C, Z80::D, Z80::E, Z80::H, Z80::L})
            if (!Out.available(R))
              Enter.addReg(R, RegState::Implicit);
          if (Wide) {
            for (MCRegister R : {Z80::BC, Z80::DE, Z80::HL})
              Out.removeReg(R);
            for (MCRegister R : {Z80::UBC, Z80::UDE, Z80::UHL})
              if (!Out.available(R))
                Enter.addReg(R, RegState::Implicit);
          }
          Read.setDesc(TII.get(Z80::LD8ap));
          Read.getOperand(0).setReg(Z80::A);
          Read.getOperand(0).setIsDead(false);
          Read.getOperand(1).setReg(Bank);
          Read.getOperand(1).setIsKill(false);
          It = std::next(Read.getIterator());
          Exits.insert(BuildMI(MBB, It, Read.getDebugLoc(),
                               TII.get(Wide ? Z80::EXX24 : Z80::EXX16)));
          if (Bridge)
            BuildMI(MBB, It, Read.getDebugLoc(), TII.get(Z80::LD8gg), Byte)
                .addReg(Z80::A, RegState::Kill);
          if (PreserveA)
            BuildMI(MBB, It, Read.getDebugLoc(), TII.get(Z80::EXAF));
          Entry.eraseFromParent();
          ++NumBankRegions;
          Changed = true;
          continue;
        }
      }
      SmallVector<MachineInstr *, 16> Body;
      MachineInstr *Exit = nullptr;
      MCRegister Bridge;
      bool Import = false, Defined = false, UsesA = false, DefinesF = false;
      bool ReadsF = false;
      bool Legal = true;
      unsigned Count = 0;
      for (auto Scan = It; Scan != MBB.end() && ++Count <= 32; ++Scan) {
        auto &MI = *Scan;
        if (MI.getOpcode() == Save) {
          if (MI.getOperand(0).getReg() == Alt &&
              MI.getOperand(1).getReg() == Main)
            Exit = &MI;
          break;
        }
        // Debug locations, stack/CFA changes, side effects and bank transfers
        // end a region. Keep all original memory accesses in their order.
        if (MI.isPseudo() || MI.isTerminator() || MI.isCall() ||
            MI.isInlineAsm() || MI.isBundled() || MI.isDebugInstr() ||
            MI.hasUnmodeledSideEffects() || MI.isCFIInstruction())
          break;
        bool DefBridge = false;
        for (const auto &MO : MI.operands()) {
          if (MO.isRegMask() || MO.isFI()) {
            Legal = false;
            break;
          }
          if (!MO.isReg() || !MO.getReg())
            continue;
          MCRegister R = MO.getReg();
          if (R == Z80::SPS || R == Z80::SPL || Z80::ALT8RegClass.contains(R) ||
              Z80::ALT16RegClass.contains(R) ||
              Z80::ALT24RegClass.contains(R)) {
            Legal = false;
            break;
          }
          UsesA |= TRI.regsOverlap(R, Z80::A);
          DefinesF |= MO.isDef() && TRI.regsOverlap(R, Z80::F);
          ReadsF |= MO.readsReg() && TRI.regsOverlap(R, Z80::F);
          if (!switched(R, TRI) || TRI.isSubRegisterEq(Main, R))
            continue;
          // One independently live byte can cross through A. A remains in the
          // primary AF bank during EXX, and is preserved when live on exit.
          if (!Z80::G8RegClass.contains(R) || (Bridge && Bridge != R)) {
            Legal = false;
            break;
          }
          Bridge = R;
          Import |= MO.readsReg() && !Defined;
          DefBridge |= MO.isDef();
        }
        Defined |= DefBridge;
        if (!Legal)
          break;
        Body.push_back(&MI);
      }
      if (!Exit || Body.empty() || !Legal || (Bridge && UsesA))
        continue;
      LiveRegUnits Out(TRI);
      Out.addUnits(LiveAfter.find(Exit)->second);
      bool ExportPair = !Out.available(Main);
      bool ExportByte = Bridge && Defined && !Out.available(Bridge);
      bool PreserveA = Bridge && !Out.available(Z80::A);
      // Saving AF must not restore over flags computed by the region.
      if (PreserveA && DefinesF)
        continue;
      bool SwapAF = PreserveA && FreeAF && !ReadsF;
      auto Map = [&](MCRegister R) -> MCRegister {
        if (R == Main)
          return Bank;
        if (unsigned Sub = TRI.getSubRegIndex(Main, R))
          return TRI.getSubReg(Bank, Sub);
        if (R == Bridge)
          return Z80::A;
        return R;
      };
      SmallVector<unsigned, 16> Opcodes;
      for (const auto *MI : Body) {
        unsigned Opc = MI->getOpcode();
        if (Opc == Z80::LD8gp && Map(MI->getOperand(0).getReg()) == Z80::A)
          Opc = Z80::LD8ap;
        if (Opc == Z80::LD8pg && Map(MI->getOperand(1).getReg()) == Z80::A)
          Opc = Z80::LD8pa;
        const auto &Desc = TII.get(Opc);
        unsigned I = 0;
        for (const auto &MO : MI->operands()) {
          if (MO.isReg() && MO.getReg()) {
            MCRegister R = Map(MO.getReg());
            if (MO.isImplicit()) {
              // Fixed implicit registers cannot be renamed without selecting
              // a different instruction (e.g. accumulator arithmetic).
              Legal &= R == MO.getReg().asMCReg();
            } else {
              const auto *RC = TII.getRegClass(Desc, I);
              Legal &= RC && RC->contains(R);
            }
          }
          ++I;
        }
        Opcodes.push_back(Opc);
      }
      if (!Legal)
        continue;
      unsigned NewCost = (Bytes ? 2 : 8) +
                         (Import + ExportByte) * (Bytes ? 1 : 4) +
                         PreserveA * (Bytes ? 2 : (SwapAF ? 8 : 21)) +
                         ExportPair * (Bytes ? 4 : 29);
      if (NewCost >= (Bytes ? 8u : 58u))
        continue;
      auto At = Entry.getIterator();
      auto Emit = [&](unsigned Opc) {
        return BuildMI(MBB, At, Entry.getDebugLoc(), TII.get(Opc));
      };
      auto CFI = [&](int Offset) {
        if (MF.needsFrameMoves() && !ST.getFrameLowering()->hasFP(MF)) {
          unsigned Idx = MF.addFrameInst(
              MCCFIInstruction::createAdjustCfaOffset(nullptr, Offset));
          Emit(TargetOpcode::CFI_INSTRUCTION).addCFIIndex(Idx);
        }
      };
      if (SwapAF)
        Emit(Z80::EXAF);
      else if (PreserveA) {
        Emit(Wide ? Z80::PUSH24AF : Z80::PUSH16AF);
        CFI(Wide ? 3 : 2);
      }
      if (Import)
        Emit(Z80::LD8gg).addDef(Z80::A).addReg(Bridge);
      auto EnterBank = Emit(Wide ? Z80::EXX24 : Z80::EXX16);
      Entries.insert(EnterBank);
      // EXX physically exchanges every pair, but old values overwritten by
      // this region are not semantic inputs. Express partial preservation as
      // byte uses instead of inventing uses of dead halves of a pair.
      LiveRegUnits Restore = Out;
      Restore.removeReg(Main);
      if (Defined && Bridge)
        Restore.removeReg(Bridge);
      for (auto &MO : EnterBank->operands())
        if (MO.isReg() && MO.isUse() &&
            (switched(MO.getReg(), TRI) ||
             (mainReg(MO.getReg()) && MO.getReg() != Alt &&
              Out.available(MO.getReg()))))
          MO.setIsUndef(true);
      for (MCRegister R : {Z80::B, Z80::C, Z80::D, Z80::E, Z80::H, Z80::L})
        if (!Restore.available(R))
          EnterBank.addReg(R, RegState::Implicit);
      if (Wide) {
        for (MCRegister R : {Z80::BC, Z80::DE, Z80::HL})
          Restore.removeReg(R);
        for (MCRegister R : {Z80::UBC, Z80::UDE, Z80::UHL})
          if (!Restore.available(R))
            EnterBank.addReg(R, RegState::Implicit);
      }
      for (unsigned I = 0; I != Body.size(); ++I) {
        auto &MI = *Body[I];
        MI.setDesc(TII.get(Opcodes[I]));
        for (auto &MO : MI.operands())
          if (MO.isReg() && MO.getReg()) {
            MO.setReg(Map(MO.getReg()));
            if (MO.isDef())
              MO.setIsDead(false);
            else
              MO.setIsKill(false);
          }
      }
      At = Exit->getIterator();
      Exits.insert(Emit(Wide ? Z80::EXX24 : Z80::EXX16));
      if (ExportByte)
        Emit(Z80::LD8gg).addDef(Bridge).addReg(Z80::A);
      if (SwapAF)
        Emit(Z80::EXAF);
      else if (PreserveA) {
        Emit(Wide ? Z80::POP24AF : Z80::POP16AF);
        CFI(Wide ? -3 : -2);
      }
      if (ExportPair)
        Emit(Load).addDef(Main).addReg(Alt);
      It = std::next(Exit->getIterator());
      Entry.eraseFromParent();
      Exit->eraseFromParent();
      ++NumBankRegions;
      Changed = true;
    }
  }
  if (!Wide)
    for (auto &MBB : MF) {
      SmallVector<std::pair<MachineInstr *, MachineInstr *>, 8> Groups;
      for (auto I = MBB.begin(); I != MBB.end();) {
        MachineInstr *First = &*I;
        if (I->getOpcode() == Z80::ALT_SAVE16 &&
            std::next(I) != MBB.end() && BankOpcode(*std::next(I)))
          ++I;
        if (!BankOpcode(*I)) {
          ++I;
          continue;
        }
        MachineInstr *Last = &*I;
        while (++I != MBB.end() && BankOpcode(*I))
          Last = &*I;
        if (I != MBB.end() && I->getOpcode() == Z80::ALT_LOAD16)
          Last = &*I++;
        Groups.emplace_back(First, Last);
      }
      if (Groups.empty())
        continue;
      DenseMap<MachineInstr *, BitVector> After;
      for (auto [First, Last] : Groups)
        After.try_emplace(Last);
      LiveRegUnits Live(TRI);
      Live.addLiveOuts(MBB);
      for (auto &MI : reverse(MBB)) {
        auto Found = After.find(&MI);
        if (Found != After.end())
          Found->second = Live.getBitVector();
        Live.stepBackward(MI);
      }
      for (auto [First, Last] : Groups) {
        LiveRegUnits Out(TRI);
        Out.addUnits(After.find(Last)->second);
        LiveRegUnits In = Out;
        auto End = std::next(Last->getIterator());
        for (auto I = End; I != First->getIterator();)
          In.stepBackward(*--I);
        auto DL = First->getDebugLoc();
        auto CFI = [&](MachineBasicBlock::iterator At, int Offset) {
          if (MF.needsFrameMoves() && !ST.getFrameLowering()->hasFP(MF)) {
            unsigned Idx = MF.addFrameInst(
                MCCFIInstruction::createAdjustCfaOffset(nullptr, Offset));
            BuildMI(MBB, At, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
                .addCFIIndex(Idx);
          }
        };
        /* Transfers bracket the bank activation: save the primary source
           before entering, or restore the primary destination after leaving.
           Resident arithmetic can then share the transfer's bank switch.
           Earlier region formation gets first choice of load/store pairs,
           so this grouping cannot hide a cheaper resident memory access. */
        if (First->getOpcode() == Z80::ALT_SAVE16) {
          BuildMI(MBB, First, DL, TII.get(Z80::PUSH16r))
              .addReg(First->getOperand(1).getReg(),
                      getUndefRegState(First->getOperand(1).isUndef()));
          CFI(First->getIterator(), 2);
        }
        MCRegister Result;
        if (Last->getOpcode() == Z80::ALT_LOAD16) {
          Result = Last->getOperand(0).getReg();
          Out.removeReg(Result);
        }
        auto Enter = BuildMI(MBB, First, DL, TII.get(Z80::EXX16));
        Entries.insert(Enter);
        for (auto &MO : Enter->operands())
          if (MO.isReg() && MO.isUse())
            MO.setIsUndef(true);
        for (MCRegister R : {Z80::B, Z80::C, Z80::D, Z80::E, Z80::H, Z80::L,
                             Z80::B_ALT, Z80::C_ALT, Z80::D_ALT, Z80::E_ALT,
                             Z80::H_ALT, Z80::L_ALT})
          if (!In.available(R))
            Enter.addReg(R, RegState::Implicit);
        for (auto I = First->getIterator(); I != End;) {
          auto &Operation = *I++;
          if (Operation.getOpcode() == Z80::ALT_SAVE16) {
            BuildMI(MBB, Operation, DL, TII.get(Z80::POP16r),
                    mainReg(Operation.getOperand(0).getReg()));
            CFI(Operation.getIterator(), -2);
            Operation.eraseFromParent();
            continue;
          }
          if (Operation.getOpcode() == Z80::ALT_LOAD16) {
            BuildMI(MBB, Operation, DL, TII.get(Z80::PUSH16r))
                .addReg(mainReg(Operation.getOperand(1).getReg()),
                        getUndefRegState(Operation.getOperand(1).isUndef()));
            CFI(Operation.getIterator(), 2);
            Operation.eraseFromParent();
            continue;
          }
          bool Low = Operation.getOpcode() == Z80::BANK_INCL16 ||
                     Operation.getOpcode() == Z80::BANK_DECL16;
          Operation.setDesc(TII.get(BankOpcode(Operation)));
          for (auto &MO : Operation.operands())
            if (MO.isReg() && MO.getReg()) {
              if (MCRegister Main = mainReg(MO.getReg()))
                MO.setReg(Main);
              if (MO.isUse())
                MO.setIsKill(false);
              else
                MO.setIsDead(false);
            }
          if (Low)
            for (unsigned Op : {0u, 1u})
              Operation.getOperand(Op).setReg(
                  TRI.getSubReg(Operation.getOperand(Op).getReg(), Z80::sub_low));
          if (Operation.getOpcode() == Z80::ADD16rr)
            TII.expandPostRAPseudo(Operation);
        }
        auto Exit = BuildMI(MBB, End, DL, TII.get(Z80::EXX16));
        Exits.insert(Exit);
        if (Result) {
          BuildMI(MBB, End, DL, TII.get(Z80::POP16r), Result);
          CFI(End, -2);
        }
        for (auto &MO : Exit->operands())
          if (MO.isReg() && MO.isUse())
            MO.setIsUndef(true);
        for (MCRegister R : {Z80::B_ALT, Z80::C_ALT, Z80::D_ALT, Z80::E_ALT,
                             Z80::H_ALT, Z80::L_ALT}) {
          MCRegister Main = mainReg(R);
          if (!Out.available(R))
            Exit.addReg(Main, RegState::Implicit);
          if (!Out.available(Main))
            Exit.addReg(R, RegState::Implicit);
        }
        ++NumBankRegions;
        Changed = true;
      }
    }
  if (!Changed)
    return false;
  for (auto &MBB : MF) {
    // Independently allocated regions can share a bank activation. Unbanked
    // arithmetic between them commutes with EXX; primary-bank operands and
    // control-flow/debug/unwind boundaries end the opportunity.
    MachineInstr *LastExit = nullptr;
    for (auto It = MBB.begin(); It != MBB.end();) {
      auto &MI = *It++;
      /* Saturating byte arithmetic expands atomically and touches only its
         declared operands and AF. An index-memory operand remains accessible
         while either bank is active, just like an ordinary accumulator op. */
      bool AtomicArithmetic = MI.getOpcode() == Z80::SADDSAT8ar ||
                              MI.getOpcode() == Z80::SADDSAT8ai ||
                              MI.getOpcode() == Z80::SADDSAT8ap;
      if (Entries.contains(&MI) && LastExit) {
        LastExit->eraseFromParent();
        MI.eraseFromParent();
        LastExit = nullptr;
        ++NumBankJoins;
      } else if (Exits.contains(&MI)) {
        LastExit = &MI;
      } else if ((MI.isPseudo() && !AtomicArithmetic) ||
                 MI.isTerminator() || MI.isCall() ||
                 MI.isInlineAsm() || MI.isBundled() ||
                 MI.hasUnmodeledSideEffects() ||
                 llvm::any_of(MI.operands(), [&](const MachineOperand &MO) {
                   return MO.isRegMask() ||
                          (MO.isReg() && MO.getReg() &&
                           (switched(MO.getReg(), TRI) ||
                            Z80::ALT8RegClass.contains(MO.getReg()) ||
                            Z80::ALT16RegClass.contains(MO.getReg()) ||
                            Z80::ALT24RegClass.contains(MO.getReg())));
                 })) {
        LastExit = nullptr;
      }
    }
  }
  return Changed;
}

char Z80BankRegionAllocator::ID = 0;
INITIALIZE_PASS(Z80BankRegionAllocator, DEBUG_TYPE, "Z80 Bank Region Allocator",
                false, false)
FunctionPass *llvm::createZ80BankRegionAllocator() {
  return new Z80BankRegionAllocator();
}
