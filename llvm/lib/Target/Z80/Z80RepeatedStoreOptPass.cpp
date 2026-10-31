//===-- Z80RepeatedStoreOptPass.cpp - Share constant addresses --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

namespace llvm {
void initializeZ80RepeatedStoreOptPassPass(PassRegistry &);
}

namespace {
class Z80RepeatedStoreOptPass : public MachineFunctionPass {
public:
  static char ID;
  Z80RepeatedStoreOptPass() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override {
    return "Z80 Repeated Store Optimization";
  }
  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace

char Z80RepeatedStoreOptPass::ID = 0;
INITIALIZE_PASS(Z80RepeatedStoreOptPass, "z80-repeated-store-opt",
                "Z80 Repeated Store Optimization", false, false)

FunctionPass *llvm::createZ80RepeatedStoreOptPass() {
  return new Z80RepeatedStoreOptPass();
}

void llvm::initializeZ80RepeatedStoreOptPass(PassRegistry &Registry) {
  initializeZ80RepeatedStoreOptPassPass(Registry);
}

bool Z80RepeatedStoreOptPass::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getSubtarget<Z80Subtarget>().is16Bit())
    return false;
  auto &MRI = MF.getRegInfo();
  const auto &TII = *MF.getSubtarget().getInstrInfo();
  bool Changed = false;
  for (auto &MBB : MF) {
    for (auto I = MBB.begin(); I != MBB.end();) {
      auto &First = *I++;
      if (First.getOpcode() != Z80::LD8ma || First.isBundled())
        continue;
      SmallVector<MachineInstr *, 8> Stores{&First};
      unsigned Distance = 0;
      for (auto J = I; J != MBB.end() && ++Distance <= 32; ++J) {
        if (J->isCall() || J->isInlineAsm() || J->isTerminator() ||
            J->isBundled())
          break;
        if (J->getOpcode() == Z80::LD8ma &&
            J->getOperand(0).isIdenticalTo(First.getOperand(0)))
          Stores.push_back(&*J);
      }
      if (Stores.size() < 2)
        continue;

      // Two stores save 12 T-states, paying for the 10-T-state address load.
      // Allocation chooses BC, DE or HL and can fold the constant back into
      // an absolute store when all pairs are occupied. No memory access moves.
      Register Address = MRI.createVirtualRegister(&Z80::G16RegClass);
      BuildMI(MBB, First, First.getDebugLoc(), TII.get(Z80::LD16ri), Address)
          .add(First.getOperand(0));
      I = std::next(Stores.back()->getIterator());
      for (MachineInstr *Store : Stores) {
        auto New =
            BuildMI(MBB, Store, Store->getDebugLoc(), TII.get(Z80::LD8pa))
                .addReg(Address)
                .addReg(Z80::A)
                .cloneMemRefs(*Store);
        New->setFlags(Store->getFlags());
        Store->eraseFromParent();
      }
      Changed = true;
    }
  }
  return Changed;
}
