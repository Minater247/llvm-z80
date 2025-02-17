
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

#define DEBUG_TYPE "z80-post-rewrite"

using namespace llvm;

namespace {
class Z80PostRewritePass : public MachineFunctionPass {
public:
  static char ID;

  using LiveRegMap = std::map<MachineInstr*, DenseSet<unsigned>>;

  LiveRegMap constructLiveRegMap(MachineFunction &MF);

  Z80PostRewritePass();

  StringRef getPassName() const override {
    return "Z80 post rewrite pass";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool matchOptimizeLoadAddCopy(MachineRegisterInfo& MRI, MachineBasicBlock& MBB, MachineInstr &MI, LiveRegMap& LRM, int64_t& NN);
};

} // end anonymous namespace

Z80PostRewritePass::LiveRegMap Z80PostRewritePass::constructLiveRegMap(MachineFunction &MF)
{
  // or iterate backwards and use LivePhysRegs?!?
  DenseSet<unsigned> LiveRegs;
  LiveRegMap LRM;
  for (auto &MBB : MF) {
    LiveRegs.clear();
    for (auto& rmp  : MBB.liveins())
      LiveRegs.insert(rmp.PhysReg);
    for (auto &MI : MBB) {
      for (MachineOperand &MO : MI.operands()) {
        if (MO.isReg()) {
          if (MO.isDef())
            LiveRegs.insert(MO.getReg());
          if (MO.isKill() || MO.isDead())
            LiveRegs.erase(MO.getReg());
        }
      }
      LRM.emplace(&MI, LiveRegs);
    }
  }
  return LRM;
}

bool Z80PostRewritePass::matchOptimizeLoadAddCopy(MachineRegisterInfo& MRI, MachineBasicBlock& MBB, MachineInstr &MI, LiveRegMap& LRM, int64_t& NN) {
  // make sure BC is not in use
  if (LRM[&MI].count(Z80::BC) != 0)
      return false;

  // Look for
  //     $iy = COPY $de
  //     $de = LD16ri NN
  //     $iy = ADD16ao killed $iy(tied-def 0), killed $de, implicit-def dead $f
  //     $de = COPY killed $iy

  auto getNextInstr = [&MBB](MachineInstr& MI) -> MachineInstr* {
    auto It = MI.getIterator();
    ++It;
    if (It == MBB.end())
        return nullptr;
    else
        return &*It;
  };

  // $iy = COPY $de
  if (MI.getOpcode() != Z80::COPY || MI.getOperand(0).getReg() != Z80::IY || MI.getOperand(1).getReg() != Z80::DE)
    return false;

  // $de = LD16ri NN
  MachineInstr *LDInst = getNextInstr(MI);
  if (!LDInst || LDInst->getOpcode() != Z80::LD16ri || LDInst->getOperand(0).getReg() != Z80::DE)
    return false;
  if (!LDInst->getOperand(1).isImm())
    return false;
  NN = LDInst->getOperand(1).getImm();
  
  // $iy = ADD16ao killed $iy(tied-def 0), killed $de, implicit-def dead $f
  MachineInstr *ADDInst = getNextInstr(*LDInst);
  if (!ADDInst || ADDInst->getOpcode() != Z80::ADD16ao || ADDInst->getOperand(1).getReg() != Z80::IY || ADDInst->getOperand(2).getReg() != Z80::DE)
    return false;

  // $de = COPY killed $iy
  MachineInstr *FinalCopy = getNextInstr(*ADDInst);
  if (!FinalCopy || FinalCopy->getOpcode() != Z80::COPY ||
      FinalCopy->getOperand(0).getReg() != Z80::DE ||
      FinalCopy->getOperand(1).getReg() != Z80::IY || !FinalCopy->getOperand(1).isKill())
    return false;

  return true;
}


Z80PostRewritePass::Z80PostRewritePass()
    : MachineFunctionPass(ID) {
}

bool Z80PostRewritePass::runOnMachineFunction(MachineFunction &MF)
{
  bool changed = false;

  auto LRM = constructLiveRegMap(MF);
  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (auto &MBB : MF) {
    for (auto MI = MBB.begin(), End = MBB.end(); MI != End; ) {
      int64_t NN;
      if (matchOptimizeLoadAddCopy(MRI, MBB, *MI, LRM, NN)) {
        MachineIRBuilder MIB(*MI);
        Register BC = Z80::BC, HL = Z80::HL;
        MIB.buildInstr(Z80::EX16DE);
        MIB.buildInstr(Z80::LD16ri).addDef(BC).addImm(NN);
        MIB.buildInstr(Z80::ADD16aa).addDef(HL).addUse(BC, RegState::Kill);
        MIB.buildInstr(Z80::EX16DE);
        MI = MBB.erase(MI, std::next(MI, 4));
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

static RegisterPass<Z80PostRewritePass> X("z80-post-rewrite", "Z80 post rewrite",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

