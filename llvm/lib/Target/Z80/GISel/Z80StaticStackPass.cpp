
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
    LLVM_DEBUG(dbgs() << F.getName().str() << " is marked reentrant, skipping\n");
    return false;
  }

  // Idea is that instead of using stack for local variables that spill,
  // we use a global variable for spilled local variables.
  // This is a lot faster as 16 bit load from global is 20 t-states,
  // while indexes access by 8-bit registers is 2*19=38 t-states.
  // 8-bit access is tricky though as there is no direct instruction
  // for them, can be done with only using A.
  //
  // The plan is to reuse the frame pointer register ix so that performance
  // change would be zero. Current code is slower. To be done.
  //
  // This hack means that recursion is not possible and functions take
  // more memory.
  //
  // XXX set up IX in function prologue pointing to the global variables

  std::map<int, size_t> addresses;
  size_t totalSize = 0;
  for (int i = 0; i < MFI.getObjectIndexEnd(); ++i) {
    if (MFI.isDeadObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "FrameIndex " << i << " dead\n");
      continue;
    }

    if (!MFI.isSpillSlotObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "FrameIndex " << i << ": not spill slot object, skipping\n");
      return false;
    }

    if (MFI.isVariableSizedObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "FrameIndex " << i << ": variable, skipping\n");
      return false;
    }

    if (MFI.isObjectPreAllocated(i)) {
      LLVM_DEBUG(dbgs() << "FrameIndex " << i << ": preallocated, skipping\n");
      return false;
    }

    if (MFI.isFixedObjectIndex(i)) {
      LLVM_DEBUG(dbgs() << "FrameIndex " << i << ": fixed, skipping\n");
      return false;
    }

    uint64_t Size = MFI.getObjectSize(i);

    addresses[i] = totalSize;
    totalSize += Size;

    LLVM_DEBUG(dbgs() << "FrameIndex " << i << ": Size=" << Size << "\n");

    MFI.RemoveStackObject(i);
  }

  if (!totalSize) {
    return false;
  }

  // If we made here then we can replace everything
  LLVM_DEBUG(dbgs() << "Found " << addresses.size() << " stack objects of total " << totalSize << " bytes, turning them into a global variable.\n");

  Module &M   = *F.getParent();
  LLVMContext &Ctx = M.getContext();

  Type *Int8Ty = Type::getInt8Ty(Ctx);
  Type *ArrayTy = ArrayType::get(Int8Ty, totalSize);

  Constant *InitVal = Constant::getNullValue(ArrayTy);

  std::string GlobalName = F.getName().str() + "__variables";

  GlobalVariable *GV = M.getGlobalVariable(GlobalName);
  if (!GV) {
    LLVM_DEBUG(dbgs() << "Create global " << GlobalName << "\n");
    GV = new GlobalVariable(
        M,
        ArrayTy,
        false,
        GlobalValue::CommonLinkage,
        InitVal,
        GlobalName
    );
    GV->setSection(".data");
  }

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

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
        LLVM_DEBUG(dbgs() << "TargetOpcode::LD88ro: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");

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
        LLVM_DEBUG(dbgs() << "TargetOpcode::LD88or: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD16mr))
          .addGlobalAddress(GV, address + offset)
          .addReg(MI.getOperand(2).getReg());
        MI.eraseFromParent();
        continue;
      }
      if (MI.getOpcode() == Z80::LD8ro && MI.getOperand(1).isFI() && MI.getOperand(2).isImm()) {
        int frame_index = MI.getOperand(1).getIndex();
        int64_t offset = MI.getOperand(2).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "TargetOpcode::LD8ro: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        if(MI.getOperand(0).getReg() != Z80::A) {
          LLVM_DEBUG(dbgs() << "MI.getOperand(0).getReg() != Z80::A\n");
          // XXX move setting up IX to the prologue of the function
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::PUSH16r))
            .addReg(Z80::IX);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD16ri))
            .addReg(Z80::IX)
            .addGlobalAddress(GV);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8ro))
            .addReg(MI.getOperand(0).getReg(), MI.getOperand(0).getTargetFlags())
            .addReg(Z80::IX)
            .addImm(address + offset);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::POP16r))
            .addReg(Z80::IX);
          MI.eraseFromParent();
          continue;
        }

        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8am))
          .addGlobalAddress(GV, address + offset);
        MI.eraseFromParent();
        continue;
      }
      if (MI.getOpcode() == Z80::LD8or && MI.getOperand(0).isFI() && MI.getOperand(1).isImm()) {
        int frame_index = MI.getOperand(0).getIndex();
        int64_t offset = MI.getOperand(1).getImm();
        auto fit = addresses.find(frame_index);
        if (fit == addresses.end())
          continue;
        size_t address = fit->second;
        LLVM_DEBUG(dbgs() << "TargetOpcode::LD8or: " << frame_index << ": staticStack+" << address << "+" << offset << "\n");
        if(MI.getOperand(2).getReg() != Z80::A) {
          LLVM_DEBUG(dbgs() << "MI.getOperand(2).getReg() != Z80::A\n");
#if 0
          // AF push based, quite slow
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::PUSH16AF));
          auto MBI = BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8gg))
            .addReg(Z80::A)
            .addReg(MI.getOperand(2).getReg(), MI.getOperand(2).getTargetFlags());
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8ma))
            .addGlobalAddress(GV, address + offset);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::POP16AF));
#else
          // IX based
          // XXX move setting up IX to the prologue of the function
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::PUSH16r))
            .addReg(Z80::IX);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD16ri))
            .addReg(Z80::IX)
            .addGlobalAddress(GV);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8or))
            .addReg(Z80::IX)
            .addImm(address + offset)
            .addReg(MI.getOperand(2).getReg(), MI.getOperand(2).getTargetFlags());
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::POP16r))
            .addReg(Z80::IX);
#endif
          MI.eraseFromParent();
          continue;
        }
        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Z80::LD8ma))
          .addGlobalAddress(GV, address + offset);
        MI.eraseFromParent();
        continue;
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

