
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

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

Z80PostRewritePass::Z80PostRewritePass() : MachineFunctionPass(ID) {}

bool Z80PostRewritePass::runOnMachineFunction(MachineFunction &MF) {
  bool changed = false;

  const auto &TII = *MF.getSubtarget().getInstrInfo();
  const auto &TRI = *MF.getSubtarget().getRegisterInfo();
  const auto &MFI = MF.getFrameInfo();
  for (auto &MBB : MF)
    for (auto &MI : llvm::make_early_inc_range(MBB))
      if (MI.getOpcode() == Z80::ADD16rr || MI.getOpcode() == Z80::BANK_ADD16 ||
          MI.getOpcode() == Z80::BANK_LD16ro ||
          MI.getOpcode() == Z80::BANK_LD16or)
        changed |= TII.expandPostRAPseudo(MI);
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
          if (Source != Dst)
            BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY))
                .add(MI.getOperand(0))
                .addReg(Source);
          MI.eraseFromParent();
          changed = true;
          break;
        }
        if (Prev.mayStore() &&
            (!Stored || PrevFI == FI || PrevFI < 0 ||
             !MFI.isSpillSlotObjectIndex(PrevFI) ||
             (!Prev.memoperands_empty() && Prev.hasOrderedMemoryRef())))
          break;
        if (Prev.modifiesRegister(Dst, &TRI))
          break;
      }
    }
  }
  // Forwarding can expose a spill overwritten before any memory access.
  // A private spill cannot be observed asynchronously; keep this deliberately
  // local and stop at unknown memory operations, calls and control flow.
  // Accesses to distinct private frame objects cannot alias this spill.
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
          Prev.eraseFromParent();
          changed = true;
          break;
        }
        if (Prev.mayLoadOrStore()) {
          if ((!TII.isLoadFromStackSlot(Prev, PrevFI) &&
               !TII.isStoreToStackSlot(Prev, PrevFI)) ||
              PrevFI == FI || PrevFI < 0 || !MFI.isSpillSlotObjectIndex(PrevFI))
            break;
        }
      }
    }
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
