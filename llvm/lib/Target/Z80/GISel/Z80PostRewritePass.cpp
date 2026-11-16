
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/SlotIndexes.h"

#define DEBUG_TYPE "z80-post-rewrite"

using namespace llvm;

namespace {
// Only complete, zero-offset frame accesses are considered below. Keep widths
// explicit: register aliases alone do not prove that all loaded bytes were
// stored.
static unsigned spillAccessWidth(unsigned Opc) {
  switch (Opc) {
  case Z80::LD8ro:
  case Z80::LD8go:
  case Z80::LD8or:
  case Z80::LD8og:
    return 1;
  case Z80::LD88ro:
  case Z80::LD88or:
  case Z80::LD16ro:
  case Z80::LD16or:
    return 2;
  case Z80::LD24ro:
  case Z80::LD24or:
    return 3;
  default:
    return 0;
  }
}

class Z80PostRewritePass : public MachineFunctionPass {
public:
  static char ID;

  Z80PostRewritePass();

  StringRef getPassName() const override { return "Z80 post rewrite pass"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool matchOptimizeLoadAddCopy(MachineBasicBlock &MBB, MachineInstr &MI,
                                bool BCDead, int64_t &NN);
};

} // end anonymous namespace

bool Z80PostRewritePass::matchOptimizeLoadAddCopy(MachineBasicBlock &MBB,
                                                  MachineInstr &MI, bool BCDead,
                                                  int64_t &NN) {
  // make sure BC is not in use
  if (!BCDead)
    return false;

  // Look for
  //     $iy = COPY $de
  //     $de = LD16ri NN
  //     $iy = ADD16ao killed $iy(tied-def 0), killed $de, implicit-def dead $f
  //     $de = COPY killed $iy

  auto getNextInstr = [&MBB](MachineInstr &MI) -> MachineInstr * {
    auto It = MI.getIterator();
    ++It;
    if (It == MBB.end())
      return nullptr;
    else
      return &*It;
  };

  // $iy = COPY $de
  if (MI.getOpcode() != Z80::COPY || MI.getOperand(0).getReg() != Z80::IY ||
      MI.getOperand(1).getReg() != Z80::DE)
    return false;

  // $de = LD16ri NN
  MachineInstr *LDInst = getNextInstr(MI);
  if (!LDInst || LDInst->getOpcode() != Z80::LD16ri ||
      LDInst->getOperand(0).getReg() != Z80::DE)
    return false;
  if (!LDInst->getOperand(1).isImm())
    return false;
  NN = LDInst->getOperand(1).getImm();

  // $iy = ADD16ao killed $iy(tied-def 0), killed $de, implicit-def dead $f
  MachineInstr *ADDInst = getNextInstr(*LDInst);
  if (!ADDInst || ADDInst->getOpcode() != Z80::ADD16ao ||
      ADDInst->getOperand(1).getReg() != Z80::IY ||
      ADDInst->getOperand(2).getReg() != Z80::DE)
    return false;

  // $de = COPY killed $iy
  MachineInstr *FinalCopy = getNextInstr(*ADDInst);
  if (!FinalCopy || FinalCopy->getOpcode() != Z80::COPY ||
      FinalCopy->getOperand(0).getReg() != Z80::DE ||
      FinalCopy->getOperand(1).getReg() != Z80::IY ||
      !FinalCopy->getOperand(1).isKill())
    return false;

  return true;
}

bool llvm::reuseZ80SpillLoads(MachineFunction &MF, SlotIndexes *Indexes) {
  bool changed = false;
  const auto &TII = *MF.getSubtarget().getInstrInfo();
  const auto &TRI = *MF.getSubtarget().getRegisterInfo();
  const auto &MFI = MF.getFrameInfo();
  // Splitting may reload a private spill while the value remains in a
  // register after an earlier load or store. Reuse it over a bounded local
  // window, copying it if necessary. This also avoids the scratch saves
  // needed to reload an index register on the original Z80.
  for (auto &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      auto &MI = *I++;
      int FI;
      Register Dst = TII.isLoadFromStackSlot(MI, FI);
      if (!Dst || FI < 0 || !MFI.isSpillSlotObjectIndex(FI) || MI.isBundled() ||
          (!MI.memoperands_empty() && MI.hasOrderedMemoryRef()))
        continue;
      auto Scan = MI.getIterator();
      for (unsigned N = 0; Scan != MBB.begin() && N != 32; ++N) {
        auto &Prev = *--Scan;
        if (Prev.isCall() || Prev.isInlineAsm() || Prev.isTerminator() ||
            Prev.isBundled() || Prev.hasUnmodeledSideEffects())
          break;
        int PrevFI;
        Register Loaded = TII.isLoadFromStackSlot(Prev, PrevFI);
        bool SameLoad = Prev.getOpcode() == MI.getOpcode() && Loaded &&
                        PrevFI == FI &&
                        (Loaded == Dst || MI.getOpcode() == Z80::LD88ro ||
                         Z80::G8RegClass.contains(Loaded, Dst));
        Register Stored = TII.isStoreToStackSlot(Prev, PrevFI);
        bool SameStore = Stored && PrevFI == FI &&
                         spillAccessWidth(MI.getOpcode()) &&
                         spillAccessWidth(Prev.getOpcode()) ==
                             spillAccessWidth(MI.getOpcode()) &&
                         !Prev.getOperand(2).isUndef();
        Register Source = SameLoad ? Loaded : SameStore ? Stored : Register();
        if (Source && Source != Dst)
          for (auto J = std::next(Scan); J != MI.getIterator(); ++J)
            if (J->modifiesRegister(Source, &TRI)) {
              SameLoad = SameStore = false;
              break;
            }
        if ((SameLoad || SameStore) &&
            (Prev.memoperands_empty() || !Prev.hasOrderedMemoryRef())) {
          // The retained value now reaches beyond the earlier last use.
          if (SameLoad)
            Prev.getOperand(0).setIsDead(false);
          for (auto J = Scan; J != MI.getIterator(); ++J)
            J->clearRegisterKills(Source, &TRI);
          if (Source != Dst) {
            auto Copy =
                BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY))
                    .add(MI.getOperand(0))
                    .addReg(Source);
            if (Indexes)
              Indexes->replaceMachineInstrInMaps(MI, *Copy);
          } else if (Indexes) {
            Indexes->removeMachineInstrFromMaps(MI);
          }
          MI.eraseFromParent();
          changed = true;
          break;
        }
        if (Prev.mayStore()) {
          bool OtherSpill = Stored && PrevFI != FI && PrevFI >= 0 &&
                            MFI.isSpillSlotObjectIndex(PrevFI);
          if ((!Prev.memoperands_empty() && Prev.hasOrderedMemoryRef()) ||
              (!OtherSpill &&
               MI.mayAlias(static_cast<AAResults *>(nullptr), Prev, false)))
            break;
        }
        if (Prev.modifiesRegister(Dst, &TRI))
          break;
      }
    }
  }
  // Forwarding can expose a spill overwritten before any memory access.
  // A private spill cannot be observed asynchronously; keep this deliberately
  // local and stop at potentially aliasing accesses, calls and control flow.
  // LLVM IR memory references cannot alias private spill objects; preserving
  // that distinction lets cursor updates remain in registers across stores.
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      int FI;
      unsigned Width = spillAccessWidth(MI.getOpcode());
      if (!Width || !TII.isStoreToStackSlot(MI, FI) || FI < 0 ||
          !MFI.isSpillSlotObjectIndex(FI) || MI.isBundled() ||
          (!MI.memoperands_empty() && MI.hasOrderedMemoryRef()))
        continue;
      auto Scan = MI.getIterator();
      for (unsigned N = 0; Scan != MBB.begin() && N != 32; ++N) {
        auto &Prev = *--Scan;
        if (Prev.isCall() || Prev.isInlineAsm() || Prev.isTerminator() ||
            Prev.isBundled() || Prev.hasUnmodeledSideEffects() ||
            (!Prev.memoperands_empty() && Prev.hasOrderedMemoryRef()))
          break;
        int PrevFI;
        if (spillAccessWidth(Prev.getOpcode()) == Width &&
            TII.isStoreToStackSlot(Prev, PrevFI) && PrevFI == FI) {
          if (Indexes)
            Indexes->removeMachineInstrFromMaps(Prev);
          Prev.eraseFromParent();
          changed = true;
          break;
        }
        if (Prev.mayLoadOrStore()) {
          if ((!TII.isLoadFromStackSlot(Prev, PrevFI) &&
               !TII.isStoreToStackSlot(Prev, PrevFI)) ||
              PrevFI == FI || PrevFI < 0 || !MFI.isSpillSlotObjectIndex(PrevFI))
            if (MI.mayAlias(static_cast<AAResults *>(nullptr), Prev, false))
              break;
        }
      }
    }
  }
  // A private spill with only stores has no remaining consumers. Include all
  // other frame operands in the read set, so address-taking and partial or
  // unrecognized accesses keep the object alive.
  SmallDenseSet<int, 16> ReadSlots;
  SmallVector<MachineInstr *, 16> Stores;
  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      int FI;
      Register Stored = TII.isStoreToStackSlot(MI, FI);
      if (Stored && FI >= 0 && MFI.isSpillSlotObjectIndex(FI) &&
          !MI.isBundled() && !MI.hasUnmodeledSideEffects() &&
          (MI.memoperands_empty() || !MI.hasOrderedMemoryRef())) {
        Stores.push_back(&MI);
        continue;
      }
      for (const auto &MO : MI.operands())
        if (MO.isFI())
          ReadSlots.insert(MO.getIndex());
    }
  for (auto *MI : Stores) {
    int FI;
    TII.isStoreToStackSlot(*MI, FI);
    if (ReadSlots.contains(FI))
      continue;
    if (Indexes)
      Indexes->removeMachineInstrFromMaps(*MI);
    MI->eraseFromParent();
    changed = true;
  }
  return changed;
}

Z80PostRewritePass::Z80PostRewritePass() : MachineFunctionPass(ID) {}

/// Place bank reloads on the edge that consumes their destination. A transfer
/// with one live successor can bypass other paths when its source and
/// destination remain unchanged through the predecessor's terminators.
static bool sinkBankReloads(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<Z80Subtarget>();
  if (!ST.is16Bit() || ST.hasEZ80Ops() ||
      !MF.getProperties().hasTracksLiveness())
    return false;
  const auto &TRI = *ST.getRegisterInfo();
  bool Changed = false;
  for (auto &MBB : llvm::make_early_inc_range(MF)) {
    if (MBB.succ_size() != 2)
      continue;
    for (auto &MI : llvm::make_early_inc_range(MBB)) {
      if (MI.getOpcode() != Z80::ALT_LOAD16 || MI.isBundled())
        continue;
      Register Dst = MI.getOperand(0).getReg();
      Register Src = MI.getOperand(1).getReg();
      bool Safe = true;
      unsigned Budget = 32;
      for (auto It = std::next(MI.getIterator()); It != MBB.end(); ++It)
        if (!Budget-- || It->isCall() || It->isInlineAsm() || It->isBundled() ||
            It->readsRegister(Dst, &TRI) || It->modifiesRegister(Dst, &TRI) ||
            It->modifiesRegister(Src, &TRI)) {
          Safe = false;
          break;
        }
      if (!Safe)
        continue;
      MachineBasicBlock *Target = nullptr;
      for (auto *Succ : MBB.successors()) {
        LiveRegUnits Live(TRI);
        Live.addLiveIns(*Succ);
        if (!Live.available(Dst)) {
          if (Target) {
            Safe = false;
            break;
          }
          Target = Succ;
        }
      }
      if (!Safe || !Target || Target == &MBB)
        continue;
      /* A loop header can have a backedge as well as this incoming edge.
         Splitting the entry keeps the transfer outside the loop, unlike
         sinking it directly into the header on every iteration. */
      auto *Edge = MBB.SplitCriticalEdge(
          Target, MachineBasicBlock::SplitCriticalEdgeAnalyses{}, nullptr,
          nullptr);
      if (!Edge)
        continue;
      SmallVector<MCPhysReg, 4> Replaced;
      for (const auto &LI : Edge->liveins())
        if (TRI.regsOverlap(LI.PhysReg, Dst))
          Replaced.push_back(LI.PhysReg);
      for (MCPhysReg R : Replaced)
        Edge->removeLiveIn(R);
      Edge->addLiveIn(Src);
      Edge->sortUniqueLiveIns();
      for (auto It = std::next(MI.getIterator()); It != MBB.end(); ++It)
        It->clearRegisterKills(Src, &TRI);
      MI.getOperand(1).setIsKill(false);
      Edge->splice(Edge->begin(), &MBB, MI.getIterator());
      Changed = true;
      break;
    }
  }
  return Changed;
}

bool Z80PostRewritePass::runOnMachineFunction(MachineFunction &MF) {
  bool changed = false;

  const auto &TII = *MF.getSubtarget().getInstrInfo();
  const auto &TRI = *MF.getSubtarget().getRegisterInfo();
  const auto &MFI = MF.getFrameInfo();
  /* Resolve transfers before frame lowering counts physical definitions.
     A coalesced known-value copy emits nothing and needs no callee save. */
  for (auto &MBB : MF)
    for (auto &MI : llvm::make_early_inc_range(MBB))
      if (MI.getOpcode() == Z80::ADD16rr || MI.getOpcode() == Z80::BANK_ADD16 ||
          MI.getOpcode() == Z80::BANK_LD16ro ||
          MI.getOpcode() == Z80::BANK_LD16or ||
          MI.getOpcode() == Z80::BANK_KNOWN16)
        changed |= TII.expandPostRAPseudo(MI);
  changed |= reuseZ80SpillLoads(MF);
  /* When both a primary pair and an index need the same bank value, load the
     primary pair first. The bank transfer then saves an index prefix, while
     the remaining primary-to-index copy has the same cost as the reverse. */
  if (MF.getSubtarget<Z80Subtarget>().is16Bit() &&
      !MF.getSubtarget<Z80Subtarget>().hasEZ80Ops())
    for (auto &MBB : MF)
      for (auto &MI : MBB) {
        if (MI.getOpcode() != Z80::ALT_LOAD16 || MI.isBundled() ||
            !Z80::I16RegClass.contains(MI.getOperand(0).getReg()))
          continue;
        auto Next = std::next(MI.getIterator());
        if (Next == MBB.end() || !Next->isCopy() || Next->isBundled() ||
            Next->getOperand(0).getSubReg() ||
            Next->getOperand(1).getSubReg() ||
            Next->getOperand(1).getReg() != MI.getOperand(0).getReg() ||
            !Z80::G16RegClass.contains(Next->getOperand(0).getReg()))
          continue;
        Register Index = MI.getOperand(0).getReg();
        Register Main = Next->getOperand(0).getReg();
        MI.getOperand(0).setReg(Main);
        Next->getOperand(0).setReg(Index);
        Next->getOperand(0).setIsDead(false);
        Next->getOperand(1).setReg(Main);
        Next->getOperand(1).setIsKill(false);
        changed = true;
      }
  // Splitting can also reload a private byte spill into two registers at a
  // loop edge. A byte copy is cheaper than the second stack load.
  for (auto &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      auto &MI = *I++;
      if (MI.getIterator() == MBB.begin() ||
          (MI.getOpcode() != Z80::LD8ro && MI.getOpcode() != Z80::LD8go) ||
          !MI.getOperand(1).isFI())
        continue;
      int FI = MI.getOperand(1).getIndex();
      auto &Prev = *std::prev(MI.getIterator());
      if (FI < 0 || !MFI.isSpillSlotObjectIndex(FI) ||
          (Prev.getOpcode() != Z80::LD8ro && Prev.getOpcode() != Z80::LD8go) ||
          !Prev.getOperand(1).isIdenticalTo(MI.getOperand(1)) ||
          !Prev.getOperand(2).isIdenticalTo(MI.getOperand(2)) ||
          !Z80::G8RegClass.contains(Prev.getOperand(0).getReg(),
                                    MI.getOperand(0).getReg()) ||
          (!MI.memoperands_empty() && MI.hasOrderedMemoryRef()) ||
          (!Prev.memoperands_empty() && Prev.hasOrderedMemoryRef()))
        continue;
      Prev.getOperand(0).setIsDead(false);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY))
          .add(MI.getOperand(0))
          .addReg(Prev.getOperand(0).getReg());
      MI.eraseFromParent();
      changed = true;
    }
  }

  // Only this copy pattern needs liveness. Avoid building a set of all live
  // registers at every instruction merely to query BC at a few candidates.
  SmallPtrSet<const MachineInstr *, 8> BCDead, HLive, LLive;
  if (MF.getProperties().hasTracksLiveness()) {
    for (auto &MBB : MF) {
      if (llvm::none_of(MBB, [](const MachineInstr &MI) {
            return MI.isCopy() && MI.getOperand(0).getReg() == Z80::IY &&
                   MI.getOperand(1).getReg() == Z80::DE;
          }))
        continue;
      LiveRegUnits Live(TRI);
      Live.addLiveOuts(MBB);
      for (const auto &MI : reverse(MBB)) {
        if (MI.isCopy() && MI.getOperand(0).getReg() == Z80::IY &&
            MI.getOperand(1).getReg() == Z80::DE && Live.available(Z80::BC)) {
          BCDead.insert(&MI);
          if (!Live.available(Z80::H))
            HLive.insert(&MI);
          if (!Live.available(Z80::L))
            LLive.insert(&MI);
        }
        Live.stepBackward(MI);
      }
    }
  }

  for (auto &MBB : MF) {
    for (auto MI = MBB.begin(), End = MBB.end(); MI != End;) {
      int64_t NN;
      if (matchOptimizeLoadAddCopy(MBB, *MI, BCDead.contains(&*MI), NN)) {
        // Exchange DE into HL, add the constant through dead BC, then
        // restore any live halves of the old HL. ADD16aa doubles HL; the
        // arbitrary-pair addition here must use ADD16ao with both inputs.
        auto Exchange = [&](bool Restore) {
          auto Ex = BuildMI(MBB, MI, MI->getDebugLoc(), TII.get(Z80::EX16DE));
          Register Saved = Restore ? Z80::DE : Z80::HL;
          for (auto &MO : Ex->operands())
            if (MO.isReg() && MO.isUse() && MO.getReg() == Saved)
              MO.setIsUndef(true);
          if (HLive.contains(&*MI))
            Ex.addReg(Restore ? Z80::D : Z80::H, RegState::Implicit);
          if (LLive.contains(&*MI))
            Ex.addReg(Restore ? Z80::E : Z80::L, RegState::Implicit);
        };
        Exchange(false);
        BuildMI(MBB, MI, MI->getDebugLoc(), TII.get(Z80::LD16ri), Z80::BC)
            .addImm(NN);
        BuildMI(MBB, MI, MI->getDebugLoc(), TII.get(Z80::ADD16ao), Z80::HL)
            .addReg(Z80::HL)
            .addReg(Z80::BC, RegState::Kill);
        Exchange(true);
        MI = MBB.erase(MI, std::next(MI, 4));
        changed = true;
        continue;
      }
      ++MI;
    }
  }

  changed |= sinkBankReloads(MF);
  return changed;
}

char Z80PostRewritePass::ID = 0;

FunctionPass *llvm::createZ80PostRewritePass() {
  return new Z80PostRewritePass();
}

static RegisterPass<Z80PostRewritePass> X("z80-post-rewrite",
                                          "Z80 post rewrite",
                                          false /* Only looks at CFG */,
                                          false /* Analysis Pass */);
