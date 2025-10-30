//=== lib/Target/Z80/Z80ExIncOptPass.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass detects sequences of the form:
// ```
// ex (sp),rr
// <0 or more stores to IX>
// inc sp
// inc sp
// ```
// emitted by spills to the stack. It transforms them into the more efficient:
// ```
// pop rr
// <0 or more stores to IX>
// ```
//
// This saves 21T per hit.
//===----------------------------------------------------------------------===//

// TODO: This pass could be expanded to cover any cases where SP is not modified before
// the double INC. Using a positive model takes more entries but is less error-prone,
// so adding more should be viable. Need to look into checking for constant SP.

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80RegisterInfo.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "z80-ex-inc-opt"

using namespace llvm;

namespace llvm {
    void initializeZ80ExIncOptPassPass(PassRegistry &);
}

namespace {
    class Z80ExIncOptPass : public MachineFunctionPass {
    public:
        static char ID;

        Z80ExIncOptPass() : MachineFunctionPass(ID) {}

        StringRef getPassName() const override {
            return "Z80 Ex Inc Optimization";
        }

        bool runOnMachineFunction(MachineFunction &MF) override;
    
    private:
        bool IsValidExSp(const MachineInstr  &MI, MCRegister &Reg, int &size);
        bool IsValidIntermediate(const MachineInstr &MI);
        bool IsIncSp(const MachineInstr &MI);
    };
}

char Z80ExIncOptPass::ID = 0;
INITIALIZE_PASS(Z80ExIncOptPass, DEBUG_TYPE, "Z80 Ex Inc Optimization", false, false)

FunctionPass *llvm::createZ80ExIncOptPass() {
    return new Z80ExIncOptPass();
}

void llvm::initializeZ80ExIncOptPass(PassRegistry &Registry) {
    initializeZ80ExIncOptPassPass(Registry);
}




bool Z80ExIncOptPass::IsValidExSp(const MachineInstr  &MI, MCRegister &Reg, int &size) {
    auto opcode = MI.getOpcode();

    if (opcode == Z80::EX16sa) {
        auto opreg = MI.getOperand(1).getReg();
        if (opreg == Z80::HL || opreg == Z80::IY) {
            size = 2;
            Reg = opreg;
            return true;
        }
    }

    return false;
}

bool Z80ExIncOptPass::IsValidIntermediate(const MachineInstr &MI) {
    auto opcode = MI.getOpcode();

    if (opcode == Z80::LD8og) {
        return true;
    }

    return false;
}

bool Z80ExIncOptPass::IsIncSp(const MachineInstr &MI) {
    return MI.getOpcode() == Z80::INC16s;
}



bool Z80ExIncOptPass::runOnMachineFunction(MachineFunction &MF) {
    LLVM_DEBUG(dbgs() << "********** Z80 Ex + Inc Optimization **********\n"
                    << "********** Function: " << MF.getName() << '\n');

    const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
    bool Changed = false;

    for (auto &MBB : MF) {
        LLVM_DEBUG(dbgs() << "=== Processing Basic Block: " << MBB.getName() << " ===");

        for (auto MI = MBB.begin(); MI != MBB.end();) {
            auto CurrentMI = MI++;

            LLVM_DEBUG({
                dbgs() << "    Inspecting: ";
                CurrentMI->print(dbgs());
                dbgs() << '\n';
            });

            MCRegister reg;
            int size;
            if (!IsValidExSp(*CurrentMI, reg, size)) continue;

            LLVM_DEBUG(dbgs() << "--  - Valid ex (sp)\n");

            auto Begin = CurrentMI;
            auto End = MI;

            while (End != MBB.end() && IsValidIntermediate(*End)) {
                LLVM_DEBUG({
                    dbgs() << "-- - Intermediate: ";
                    End->print(dbgs());
                    dbgs() << '\n';
                });

                ++End;
            }
            
            // TODO: look for `size` incs, rather than hardcoded 2
            //       this does not work on ez80
            if (End == MBB.end() || !IsIncSp(*End)) continue;
            auto Inc1 = End++;
            if (End == MBB.end() || !IsIncSp(*End)) continue;
            auto Inc2 = End++;

            LLVM_DEBUG(dbgs() << "Full transformation located!\n");

            BuildMI(MBB, Begin, Begin->getDebugLoc(), TII->get(Z80::POP16r), reg);

            Begin->eraseFromParent();
            Inc1->eraseFromParent();
            Inc2->eraseFromParent();

            MI = End;
            Changed = true;
        }
    }

    return Changed;
}