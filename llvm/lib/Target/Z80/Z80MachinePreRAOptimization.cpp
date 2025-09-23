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
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveRegUnits.h"
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
class Z80MachinePreRAOptimization : public MachineFunctionPass {
public:
  static char ID;

  Z80MachinePreRAOptimization() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Z80 Machine Pre-RA Optimization";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

bool Z80MachinePreRAOptimization::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallSet<Register, 16> FoldAsLoadDefCandidates;

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
            if (MI->shouldUpdateCallSiteInfo())
              MF.moveCallSiteInfo(MI, FoldMI);
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

  auto getPtrInfo = [&](Register PtrReg) -> Optional<PtrInfo> {
    auto It = PtrCache.find(PtrReg);
    if (It != PtrCache.end())
      return It->second;
    if (!PtrReg.isVirtual())
      return None;
    MachineInstr *PtrDef = MRI.getVRegDef(PtrReg);
    if (!PtrDef)
      return None;
    MachineInstr *CopyMI = nullptr;
    MachineInstr *ImmMI = PtrDef;
    if (PtrDef->getOpcode() == TargetOpcode::COPY) {
      if (!PtrDef->getOperand(1).isReg())
        return None;
      Register SrcReg = PtrDef->getOperand(1).getReg();
      if (!SrcReg.isVirtual())
        return None;
      CopyMI = PtrDef;
      ImmMI = MRI.getVRegDef(SrcReg);
      if (!ImmMI)
        return None;
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
      return None;
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
      return None;

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
    Optional<PtrInfo> PtrDetail;
    int64_t Addr = 0;
    bool UsesPointer = false;
    bool UsesImmValue = false;
    int64_t ImmValue = 0;
    Register ValReg;
    bool ValIsKill = false;
  };

  auto getStoreInfo = [&](MachineInstr &MI) -> Optional<StoreInfo> {
    StoreInfo Info;
    Info.MI = &MI;
    Info.Opcode = MI.getOpcode();
    switch (Info.Opcode) {
    case Z80::LD8pg: {
      Info.UsesPointer = true;
      Info.PtrReg = MI.getOperand(0).getReg();
      auto Ptr = getPtrInfo(Info.PtrReg);
      if (!Ptr)
        return None;
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
        return None;
      Info.PtrDetail = Ptr;
      Info.Addr = Ptr->Imm;
      Info.UsesImmValue = true;
      Info.ImmValue = MI.getOperand(1).getImm();
      return Info;
    }
    case Z80::LD8ma: {
      if (!MI.getOperand(0).isImm())
        return None;
      Info.Addr = MI.getOperand(0).getImm();
      return Info;
    }
    default:
      return None;
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

      Optional<StoreInfo> SecondInfoOpt = getStoreInfo(SecondStore);
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
      Optional<StoreInfo> BaseInfoOpt = getStoreInfo(BaseStore);
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
      Optional<PtrInfo> SecondPtrInfo = SecondInfo.PtrDetail;

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

  return Changed;
}

char Z80MachinePreRAOptimization::ID = 0;
INITIALIZE_PASS(Z80MachinePreRAOptimization, DEBUG_TYPE,
                "Optimize Z80 machine instrs before regalloc", false, false)

FunctionPass *llvm::createZ80MachinePreRAOptimizationPass() {
  return new Z80MachinePreRAOptimization();
}
