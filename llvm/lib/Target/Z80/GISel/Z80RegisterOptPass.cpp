
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

#define DEBUG_TYPE "z80-register-opt"

using namespace llvm;

namespace {
class Z80RegisterOptPass : public MachineFunctionPass {
public:
  static char ID;

  Z80RegisterOptPass();

  StringRef getPassName() const override { return "Z80 register optimization"; }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

Z80RegisterOptPass::Z80RegisterOptPass() : MachineFunctionPass(ID) {}

/// Keep a byte load with its following unit cursor update. Each old pointer
/// must have exactly these two uses in one block; the updated pointer may be
/// used after the load or by successor blocks. Memory accesses remain in place.
static bool combinePostIncrementLoads(MachineFunction &MF) {
  const auto &ST = MF.getSubtarget<Z80Subtarget>();
  /* The fused form currently addresses primary pairs. Separate operations
     retain alternate-bank choices when those registers are available. */
  if (!ST.is16Bit() || ST.hasEZ80Ops() || ST.useAlternateRegs())
    return false;
  auto &MRI = MF.getRegInfo();
  const auto &TII = *ST.getInstrInfo();
  bool Changed = false;
  for (auto &MBB : MF) {
    for (auto It = MBB.begin(); It != MBB.end();) {
      MachineInstr &Load = *It++;
      unsigned Opc = Load.getOpcode();
      if ((Opc != Z80::LD8gp && Opc != Z80::LD8rp && Opc != Z80::LD8ap) ||
          Load.isBundled())
        continue;
      Register Ptr = Load.getOperand(1).getReg();
      if (!Ptr.isVirtual() || Load.getOperand(1).getSubReg())
        continue;
      MachineInstr *Inc = nullptr;
      bool Eligible = true;
      for (auto &Use : MRI.use_nodbg_operands(Ptr)) {
        auto *MI = Use.getParent();
        if (MI == &Load)
          continue;
        if (Inc || MI->getOpcode() != Z80::INC16r || Use.getSubReg() ||
            MI->getParent() != &MBB || MI->isBundled()) {
          Eligible = false;
          break;
        }
        Inc = MI;
      }
      if (!Eligible || !Inc)
        continue;
      Register Next = Inc->getOperand(0).getReg();
      if (!Next.isVirtual() || Inc->getOperand(0).getSubReg() ||
          MRI.use_nodbg_empty(Next))
        continue;
      /* Moving a pure update after the read is legal only when none of its
         consumers precedes that read. Limit the scan to a small local region. */
      auto Scan = Inc->getIterator();
      bool Found = false;
      unsigned Distance = 0;
      for (++Scan; Scan != MBB.end() && Distance++ != 32; ++Scan) {
        if (&*Scan == &Load) {
          Found = true;
          break;
        }
        if (Scan->readsRegister(Next, ST.getRegisterInfo()))
          break;
      }
      if (!Found) {
        Scan = Load.getIterator();
        Distance = 0;
        for (++Scan; Scan != MBB.end() && Distance++ != 32; ++Scan)
          if (&*Scan == Inc) {
            Found = true;
            break;
          }
      }
      if (!Found)
        continue;
      Register Value = Load.getOperand(0).getReg();
      BuildMI(MBB, Load, Load.getDebugLoc(),
              TII.get(Opc == Z80::LD8ap ? Z80::LD8A_POST16 : Z80::LD8_POST16))
          .addDef(Value)
          .addDef(Next)
          .addReg(Ptr)
          .cloneMemRefs(Load);
      if (It != MBB.end() && &*It == Inc)
        ++It;
      Load.eraseFromParent();
      Inc->eraseFromParent();
      MRI.recomputeRegClass(Ptr);
      MRI.recomputeRegClass(Next);
      const auto *PtrRC =
          Opc == Z80::LD8ap ? &Z80::R16RegClass : &Z80::A16RegClass;
      MRI.constrainRegClass(Ptr, PtrRC);
      MRI.constrainRegClass(Next, PtrRC);
      Changed = true;
    }
  }
  return Changed;
}

bool Z80RegisterOptPass::runOnMachineFunction(MachineFunction &MF) {
  bool changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  SmallSet<Register, 16> Addresses;

  for (auto &MBB : MF) {
    // Fixed accumulator values cannot be spilled by assigning a virtual byte
    // elsewhere. Track them backwards and conservatively preserve A across
    // outgoing edges until an explicit definition proves it is available.
    bool LiveA = !MBB.succ_empty();
    for (auto &MI : llvm::reverse(MBB)) {
      if (MI.isDebugInstr())
        continue;
      MachineInstr *Previous = MI.getPrevNode();
      while (Previous && Previous->isDebugInstr())
        Previous = Previous->getPrevNode();
      auto Opc = MI.getOpcode();
      if (Opc == Z80::ADD16ao && MF.getSubtarget<Z80Subtarget>().is16Bit() &&
          !MF.getSubtarget<Z80Subtarget>().hasEZ80Ops() && !MI.isBundled()) {
        MI.setDesc(TII->get(Z80::ADD16rr));
        for (unsigned Op : {0u, 1u}) {
          Register R = MI.getOperand(Op).getReg();
          if (R.isVirtual())
            Addresses.insert(R);
        }
        changed = true;
      }
      bool Load = Opc == Z80::LD8rp || Opc == Z80::LD8gp;
      bool Store = Opc == Z80::LD8pr || Opc == Z80::LD8pg;
      if (Store && Previous && !MI.isBundled() && !Previous->isBundled()) {
        unsigned LoadOpc = Previous->getOpcode();
        Register Value = MI.getOperand(1).getReg();
        // A byte transferred directly between memory locations can use A,
        // exposing BC/DE addressing before allocation. Keep both accesses in
        // place, including volatile accesses; no memory ordering changes.
        if ((LoadOpc == Z80::LD8rp || LoadOpc == Z80::LD8gp ||
             LoadOpc == Z80::LD8ro || LoadOpc == Z80::LD8go) &&
            Value.isVirtual() && !MI.getOperand(1).getSubReg() &&
            !MI.getOperand(1).isUndef() &&
            Previous->getOperand(0).getReg() == Value &&
            !Previous->getOperand(0).getSubReg() &&
            MRI.hasOneNonDBGUse(Value) &&
            MRI.getRegClass(Value)->contains(Z80::A) && !LiveA) {
          MRI.setRegClass(Value, &Z80::A8RegClass);
          if (LoadOpc == Z80::LD8rp || LoadOpc == Z80::LD8gp) {
            Previous->setDesc(TII->get(Z80::LD8ap));
            Register Address = Previous->getOperand(1).getReg();
            if (Address.isVirtual())
              Addresses.insert(Address);
          }
          changed = true;
        }
      }
      if ((Load || Store) && !MI.isBundled()) {
        Register Value = MI.getOperand(Load ? 0 : 1).getReg();
        if (Value == Z80::A ||
            (Value.isVirtual() && MRI.getRegClass(Value) == &Z80::A8RegClass)) {
          MI.setDesc(TII->get(Load ? Z80::LD8ap : Z80::LD8pa));
          Register Address = MI.getOperand(Load ? 1 : 0).getReg();
          if (Address.isVirtual())
            Addresses.insert(Address);
          changed = true;
        }
      }
      if (MI.modifiesRegister(Z80::A, TRI))
        LiveA = false;
      if (MI.readsRegister(Z80::A, TRI))
        LiveA = true;
    }
  }
  // Recompute from every remaining use: arithmetic may still require HL or
  // an index pair, while pure accumulator accesses can admit BC and DE.
  for (Register Address : Addresses)
    MRI.recomputeRegClass(Address);
  return combinePostIncrementLoads(MF) || changed;
}

char Z80RegisterOptPass::ID = 0;

FunctionPass *llvm::createZ80RegisterOptPass() {
  return new Z80RegisterOptPass();
}

static RegisterPass<Z80RegisterOptPass> X("z80-register-opt",
                                          "Z80 register optimization",
                                          false /* Only looks at CFG */,
                                          false /* Analysis Pass */);
