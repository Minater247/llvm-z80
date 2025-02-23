
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "Z80MachineFunctionInfo.h"

#define DEBUG_TYPE "z80-static-stack"

using namespace llvm;

namespace {
class Z80StaticStackPass : public MachineFunctionPass {
public:
  static char ID;

  Z80StaticStackPass();

  StringRef getPassName() const override {
    return "Z80 static stack pass";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace


Z80StaticStackPass::Z80StaticStackPass()
    : MachineFunctionPass(ID) {
}

bool Z80StaticStackPass::runOnMachineFunction(MachineFunction &MF)
{
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto& F = MF.getFunction();
  if (F.hasFnAttribute("reentrant")) {
    LLVM_DEBUG(dbgs() << "Z80StaticStackPass: " << F.getName().str() << " is marked reentrant, aborting\n");
    return false;
  }

  // Idea is that instead of using stack for local variables that spill,
  // we use a global variable for spilled local variables.
  // This is a lot faster as 16 bit load from global is 20 t-states,
  // while offset access using IX by 8-bit registers is 2*19=38 t-states.
  //
  // 8-bit access is tricky though as there is no direct instruction
  // for them -- it can be done with only using A. So there is no
  // dramatic speedup for 8-bit access and it depends on the
  // existing code already having allocated A for the operation.
  //
  // Only spilled values are moved into globals, all other stack
  // objects are left in the stack. If all stack objects are moved into
  // global, stack is eliminated.
  //
  // If there is no need for stack and we have 8-bit access using other than
  // register A then we use the frame pointer register IX placing the global
  // into it and accessing bytes using an offset load operation.
  //
  // Limitation: this hack means that recursion is not possible and
  // functions take more memory. If recursion is needed, the function must
  // be marked with __attribute__((reentrant)). This disables that
  // optimization. Using recursion without that attribute will lead to
  // undefined behavior -- there is no detection for such a situation.

  bool canTransformEverything = true;
  for (int i = MFI.getObjectIndexBegin(); i < MFI.getObjectIndexEnd(); ++i) {
    if (MFI.isDeadObjectIndex(i)) {
      continue;
    }
    if (MFI.isVariableSizedObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << ": variable, aborting\n");
      return false;
    }
    if (!MFI.isSpillSlotObjectIndex(i) && !MFI.isObjectPreAllocated(i)) {
      canTransformEverything = false;
    }
  }

  if (!canTransformEverything) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: can't transform everything, aborting\n");
      return false;
  }

  std::map<int, size_t> addresses;
  size_t totalSize = 0;
  for (int i = MFI.getObjectIndexBegin(); i < MFI.getObjectIndexEnd(); ++i) {
    if (MFI.isDeadObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << " dead\n");
      continue;
    }

    if (MFI.isVariableSizedObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << ": variable, skipping\n");
      return false;
    }

#if 0
    if (MFI.isObjectPreAllocated(i)) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << ": preallocated, skipping\n");
      continue;
    }
#endif

    if (MFI.isFixedObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << ": fixed, skipping\n");
      continue;
    }

    if (!MFI.isSpillSlotObjectIndex(i) && !MFI.isObjectPreAllocated(i)) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << ": not spill slot or preallocated object, skipping\n");
      continue;
    }

    uint64_t Size = MFI.getObjectSize(i);
    if (!canTransformEverything && Size == 1) {
      LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << ": Size 1 and !canTransformEverything, leaving this in stack\n");
      continue;
    }

    addresses[i] = totalSize;
    totalSize += Size;

    LLVM_DEBUG(dbgs() << "Z80StaticStackPass: FrameIndex " << i << ": Size=" << Size << "\n");

    MFI.RemoveStackObject(i);
  }

  if (!totalSize) {
    return false;
  }

  // If we made here then we can replace everything
  LLVM_DEBUG(dbgs() << "Z80StaticStackPass: Found " << addresses.size() << " stack objects of total " << totalSize << " bytes, turning them into a global variable.\n");

  Module &M   = *F.getParent();
  LLVMContext &Ctx = M.getContext();

  Type *Int8Ty = Type::getInt8Ty(Ctx);
  Type *ArrayTy = ArrayType::get(Int8Ty, totalSize);

  Constant *InitVal = Constant::getNullValue(ArrayTy);

  std::string GlobalName = F.getName().str() + "__variables";

  GlobalVariable *GV = M.getGlobalVariable(GlobalName);
  if (!GV) {
    LLVM_DEBUG(dbgs() << "Z80StaticStackPass: Create global " << GlobalName << "\n");
    GV = new GlobalVariable(
        M,
        ArrayTy,
        false,
        GlobalValue::CommonLinkage,
        InitVal,
        GlobalName
    );
    GV->setSection(".data");
    GV->setAlignment(llvm::Align(1));
  }

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  bool need_IX = false;

  for (auto &MBB : MF) {
    for (auto MII = MBB.begin(), E = MBB.end(); MII != E; ) {
      MachineInstr &MI = *MII++;
      if (MI.getOpcode() == Z80::LD88ro && MI.getOperand(1).isFI() && MI.getOperand(2).isImm()) {
        const MachineOperand &DestOp = MI.getOperand(0);
        int frame_index = MI.getOperand(1).getIndex();
        int64_t offset = MI.getOperand(2).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "Z80StaticStackPass: TargetOpcode::LD88ro: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");

        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD16rm))
          .addReg(DestOp.getReg(), RegState::Define)    // Destination register
          .addGlobalAddress(GV, address + offset);
        MI.eraseFromParent();
        continue;
      }
      if (MI.getOpcode() == Z80::LD88or && MI.getOperand(0).isFI() && MI.getOperand(1).isImm()) {
        int frame_index = MI.getOperand(0).getIndex();
        int64_t offset = MI.getOperand(1).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "Z80StaticStackPass: TargetOpcode::LD88or: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD16mr))
          .addGlobalAddress(GV, address + offset)
          .addReg(MI.getOperand(2).getReg());
        MI.eraseFromParent();
        continue;
      }
      if ((MI.getOpcode() == Z80::LD8ro || MI.getOpcode() == Z80::LD8go) && MI.getOperand(1).isFI() && MI.getOperand(2).isImm()) {
        int frame_index = MI.getOperand(1).getIndex();
        int64_t offset = MI.getOperand(2).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "Z80StaticStackPass: TargetOpcode::LD8ro: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        if(MI.getOperand(0).getReg() != Z80::A) {
          LLVM_DEBUG(dbgs() << "Z80StaticStackPass: MI.getOperand(0).getReg() != Z80::A\n");
          assert(canTransformEverything);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8ro))
            .addReg(MI.getOperand(0).getReg(), RegState::Define)
            .addReg(Z80::IX)
            .addImm(address + offset);
          MI.eraseFromParent();
          need_IX = true;
          continue;
        }

        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8am))
          .addGlobalAddress(GV, address + offset);
        MI.eraseFromParent();
        continue;
      }
      if ((MI.getOpcode() == Z80::LD8or || MI.getOpcode() == Z80::LD8og) && MI.getOperand(0).isFI() && MI.getOperand(1).isImm()) {
        int frame_index = MI.getOperand(0).getIndex();
        int64_t offset = MI.getOperand(1).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "Z80StaticStackPass: TargetOpcode::LD8or: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        if(MI.getOperand(2).getReg() != Z80::A) {
          LLVM_DEBUG(dbgs() << "Z80StaticStackPass: MI.getOperand(2).getReg() != Z80::A\n");
          assert(canTransformEverything);
          // IX based
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8or))
            .addReg(Z80::IX)
            .addImm(address + offset)
            .addReg(MI.getOperand(2).getReg(), MI.getOperand(2).getTargetFlags());
          need_IX = true;
          MI.eraseFromParent();
          continue;
        }
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8ma))
          .addGlobalAddress(GV, address + offset);
        MI.eraseFromParent();
        continue;
      }
      // $hl = LEA16ro %stack.0.y, 0
      if (MI.getOpcode() == Z80::LEA16ro && MI.getOperand(1).isFI() &&  MI.getOperand(2).isImm()) {
        int frame_index = MI.getOperand(1).getIndex();
        int64_t offset = MI.getOperand(2).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "Z80StaticStackPass: TargetOpcode::LEA16ro: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD16ri))
          .addReg(MI.getOperand(0).getReg(), RegState::Define)
          .addGlobalAddress(GV, address + offset);
        MI.eraseFromParent();
        continue;
      }
      // LD8oi %stack.1.buf, 0, 0 :: (store (s8) into %ir.buf)

      if (MI.getOpcode() == Z80::LD8oi && MI.getOperand(0).isFI() && MI.getOperand(1).isImm()) {
        int frame_index = MI.getOperand(0).getIndex();
        int64_t offset = MI.getOperand(1).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "Z80StaticStackPass: TargetOpcode::LD8oi: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        assert(canTransformEverything);
        need_IX = true;
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8oi))
          .addReg(Z80::IX)
          .addImm(address + offset)
          .addImm(MI.getOperand(2).getImm());
        MI.eraseFromParent();
        continue;
      }

      if ((MI.getOpcode() == Z80::INC8o || MI.getOpcode() == Z80::DEC8o) && MI.getOperand(0).isFI() && MI.getOperand(1).isImm()) {
        int frame_index = MI.getOperand(0).getIndex();
        int64_t offset = MI.getOperand(1).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "Z80StaticStackPass: TargetOpcode::INC8o/DEC8o: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        assert(canTransformEverything);
        need_IX = true;
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(MI.getOpcode()))
          .addReg(Z80::IX)
          .addImm(address + offset);
        MI.eraseFromParent();
        continue;
      }
    }
  }

  if (need_IX) {
    assert(canTransformEverything);
    F.addFnAttr("static_stack_needs_ix");
    // XXX move this into Z80MachineFunctionInfo instead of being an attribute
    LLVM_DEBUG(dbgs() << "Z80StaticStackPass: Setting static_stack_needs_ix attribute on the function\n");
  }

  if (canTransformEverything) {
    MF.getInfo<Z80MachineFunctionInfo>()->setHasIllegalLEA(false);
  }

  for (auto &MBB : MF) {
    for (auto& MI : MBB) {
      for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
        const MachineOperand &MO = MI.getOperand(i);
        if (MO.isFI()) {
          int FrameIndex = MO.getIndex();
          if (addresses.count(FrameIndex) != 0) {
            llvm::errs() << "Offending instruction:\n";
            MI.dump();
            llvm_unreachable("Z80StaticStackPass: Eliminated FrameIndex is still being used, unhandled instruction!");
          }
        }
      }
    }
  }

  return true;
}

char Z80StaticStackPass::ID = 0;

FunctionPass *llvm::createZ80StaticStackPass() {
  return new Z80StaticStackPass();
}

static RegisterPass<Z80StaticStackPass> X(DEBUG_TYPE, "Z80 static stack pass",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

