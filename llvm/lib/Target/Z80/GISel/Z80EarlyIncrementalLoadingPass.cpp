
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
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Constants.h"

#define DEBUG_TYPE "z80-early-incremental-loading"

using namespace llvm;

namespace {
class Z80EarlyIncrementalLoadingPass: public MachineFunctionPass {
public:
  static char ID;

  Z80EarlyIncrementalLoadingPass();

  StringRef getPassName() const override {
    return "Z80 early incremental loading pass";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

bool isMemcpy(llvm::MachineOperand op) {
  if (!op.isSymbol())
      return false;
  StringRef FuncName = op.getSymbolName();
  if (!FuncName.startswith("_memcpy"))
      return false;

  // Check if the remaining part is a valid number
  StringRef Suffix = FuncName.drop_front(7); // Drop "_memcpy"
  if (Suffix.empty())
      return true;

  return Suffix.size() == 2 && Suffix.find_first_not_of("0123456789") == StringRef::npos;
}
} // end anonymous namespace


Z80EarlyIncrementalLoadingPass::Z80EarlyIncrementalLoadingPass()
    : MachineFunctionPass(ID) {
}

bool Z80EarlyIncrementalLoadingPass::runOnMachineFunction(MachineFunction &MF)
{
  bool changes = false;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  struct RegisterEntry {
  private:
    const TargetRegisterInfo *TRI;
    Register reg;
    MachineInstr* lastDef = nullptr;
    MachineInstr* lastUse = nullptr;
    Optional<std::pair<Register, int64_t>> regOff;
    Optional<int64_t> immediate;
    Register virtualCopy;

  public:
    RegisterEntry(const TargetRegisterInfo *TRI, Register reg) : TRI(TRI), reg(reg) {}

    void clearLastDeadOrKill()
    {
      assert(lastDef || lastUse);
      if (lastUse) {
        for (auto& MO : lastUse->operands()) {
          if (MO.isReg() && MO.getReg() == reg && MO.isKill()) {
            MO.setIsKill(false);
            LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: clearing kill for " << getName() << " from: "; lastUse->dump());
          }
        }
      } else if (lastDef) {
        for (auto& MO : lastDef->operands()) {
          if (MO.isReg() && MO.getReg() == reg && MO.isDead()) {
            MO.setIsDead(false);
            LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: clearing kill for " << getName() << " from: "; lastDef->dump());
          }
        }
      }
    }

    Register getReg() {
      return reg;
    }

    Register getVirtualCopy() {
      if (virtualCopy) {
        return virtualCopy;
      }
      assert(!reg.isVirtual());
      assert(lastDef);
      MachineIRBuilder MIB(*std::next(lastDef->getIterator()));
      auto *MRI = MIB.getMRI();
      virtualCopy = MRI->createGenericVirtualRegister(LLT::pointer(0, 16));
      MachineInstr *COPY = MIB.buildCopy(virtualCopy, reg);
      clearLastDeadOrKill();
      COPY->getOperand(1).setIsKill(true);
      use(*COPY);
      LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: built virtual copy: "; COPY->dump());
      return virtualCopy;
    }

    MachineInstr *getLastDef() {
      return lastDef;
    }

    MachineInstr *getLastUse() {
      return lastUse;
    }

    bool isImm() {
      return immediate != None;
    }

    int64_t getImm() {
      assert(immediate);
      return *immediate;
    }

    bool isRegOff() {
      return regOff != None;
    }

    const std::pair<Register, int64_t>& getRegOff() {
      assert(regOff);
      return *regOff;
    }

    static std::string getName(const TargetRegisterInfo *TRI, Register Reg) {
      if (Reg.isVirtual()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%%%u", Register::virtReg2Index(Reg));
        return buf;
      } else {
        return TRI->getName(Reg);
      }
    }

    std::string getName(Register Reg) const {
      return getName(TRI, Reg);
    }

    std::string getName() const {
      return getName(reg);
    }

    bool isSet() const {
      return regOff || immediate;
    }

    void reset()
    {
      if (isSet()) {
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: clearing " << getName() << "\n");
      }
      immediate.reset();
      regOff.reset();
      lastUse = nullptr;
      lastDef = nullptr;
    }

  private:
    void _def(MachineInstr& MI)
    {
      lastUse = nullptr;
      lastDef = &MI;
      virtualCopy = Register();
    }

  public:
    void def(MachineInstr& MI, Register Reg, int64_t offset) {
      if (Reg == reg) {
        reset();
        return;
      }
      LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << getName(Reg) << " + " << offset << "\n");
      regOff.emplace(Reg, offset);
      immediate.reset();
      _def(MI);
    }

    void setImm(MachineInstr& MI, int64_t i) {
      LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << i << "\n");
      regOff.reset();
      immediate = i;
      _def(MI);
    }

    void inc(MachineInstr& MI) {
      if (immediate) {
        *immediate = *immediate + 1;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << *immediate << "\n");
      } else if (regOff) {
        regOff->second = regOff->second + 1;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
      }
      _def(MI);
    }

    void add(MachineInstr& MI, int64_t value) {
      if (immediate) {
        *immediate = *immediate + value;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << *immediate << "\n");
      } else if (regOff) {
        regOff->second = regOff->second + value;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
      }
      _def(MI);
    }

    void sub(MachineInstr& MI, int64_t value) {
      if (immediate) {
        *immediate = *immediate - value;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << *immediate << "\n");
      } else if (regOff) {
        regOff->second = regOff->second - value;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
      }
      _def(MI);
    }

    void dec(MachineInstr& MI) {
      if (immediate) {
        *immediate = *immediate - 1;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << *immediate << "\n");
      } else if (regOff) {
        regOff->second = regOff->second - 1;
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
      }
      _def(MI);
    }

    void ex(MachineInstr& MI, RegisterEntry& RE)
    {
      RegisterEntry RECopy = RE;
      if (immediate) {
        RE.setImm(MI, *immediate);
      } else if (regOff) {
        RE.def(MI, regOff->first, regOff->second);
      } else {
        RE.reset();
      }
      if (RECopy.isImm()) {
        setImm(MI, RECopy.getImm());
      } else if (RECopy.isRegOff()) {
        def(MI, RECopy.getRegOff().first, RECopy.getRegOff().second);
      } else {
        reset();
      }
    }

    void use(MachineInstr& MI) {
      if (lastUse == &MI)
          return;
      lastUse = &MI;
      LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: " << getName(reg) << " used\n");
    }

    void clearLastKill() {
      assert(lastUse);
      for (auto& MO : lastUse->operands()) {
        if (MO.isReg() && MO.getReg() == reg && MO.isKill()) {
          MO.setIsKill(false);
          LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: cleared kill in "; lastUse->dump());
        }
      }
    }

    bool isRelated(Register r) {
      if (reg == r) {
        return true;
      }
      if (!r.isVirtual() && !reg.isVirtual()) {
        if (TRI->isSubRegister(reg, r) || TRI->isSubRegister(r, reg)) {
          return true;
        }
      }
      if (regOff) {
        if (regOff->first == r) {
          return true;
        }
        if (!r.isVirtual() && !regOff->first.isVirtual()) {
          if (TRI->isSubRegister(regOff->first, r) || TRI->isSubRegister(r, regOff->first)) {
            return true;
          }
        }
      }
      return false;
    }
  };

  for (auto& MBB : MF) {
    std::map<Register, RegisterEntry> Regs;

    auto getReg = [&](Register Reg) -> RegisterEntry& {
      assert(Reg);
      auto fit = Regs.find(Reg);
      if (fit != Regs.end())
          return fit->second;
      return Regs.emplace(Reg, RegisterEntry(TRI, Reg)).first->second;
    };

    auto clobber = [&](Register Reg, bool ClobberSelf = true) {
      assert(Reg);
      for (auto& e : Regs) {
        if (!ClobberSelf && e.first == Reg) {
          continue;
        }
        if (e.second.isRelated(Reg)) {
          e.second.reset();
        }
      }
    };

    auto extractImmediate = [](MachineOperand& MO) -> Optional<int64_t> {
      if (MO.isImm()) {
        return MO.getImm();
      } else if (MO.isCImm()) {
        return MO.getCImm()->getSExtValue();
      } else {
        return None;
      }
    };

    for (auto MII = MBB.begin(), E = MBB.end(); MII != E; ) {
      MachineInstr &MI = *MII++;
      LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: "; MI.dump());

      bool HasRegMask = false;
      for (auto& MO : MI.operands()) {
        if (MO.isRegMask()) {
          LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: encountered regmask, clearing all: "; MI.dump());
          Regs.clear();
          HasRegMask = true;
          break;
        }
      }
      if (HasRegMask) {
        continue;
      }

#if 0
      if (MI.getDesc().isBranch() || MI.getDesc().isBarrier()) {
        LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: branch/barrier: "; MI.dump());
        Regs.clear();
        continue;
      }
#endif

      // try to fold some thing
      if (MII != MBB.end() && std::next(MII) != MBB.end()) {
        auto& MI0 = MI;
        //auto& MI1 = *MII;
        //auto& MI2 = *std::next(MII);

        bool applied = false;

        do {
          // We look for
          //    %11:_(p0) = G_PTR_ADD %3:_, %10:_(s16)
          // And try to replace with known value or offset from a physical register
          if (MI0.getOpcode() != TargetOpcode::G_PTR_ADD)
            break;
          Register Dst = MI0.getOperand(0).getReg();
          Register Src = MI0.getOperand(1).getReg();
          Register Off = MI0.getOperand(2).getReg();
          auto& DstRE = getReg(Dst);
          auto& SrcRE = getReg(Src);
          auto OffImmValue = getIConstantVRegValWithLookThrough(Off, MRI);
          if (!OffImmValue)
            break;
          int64_t Offset = OffImmValue->Value.getSExtValue();

          Register base;
          if (!SrcRE.isSet()) {
            base = Src;
          } else if (SrcRE.isRegOff()) {
            base = SrcRE.getRegOff().first;
            Offset = SrcRE.getRegOff().second + Offset;
          } else {
            break;
          }
          for (auto& re : Regs) {
            if (re.first.isVirtual())
              continue;
            auto& RE = re.second;
            if (RE.isRegOff()) {
              if (RE.getRegOff().first == base) {
                int64_t Delta = Offset - RE.getRegOff().second;
                Register copy = RE.getVirtualCopy();
                // register copy and destination values
                getReg(copy).def(*RE.getLastUse(), RE.getRegOff().first, RE.getRegOff().second);
                DstRE.def(MI0, RE.getRegOff().first, RE.getRegOff().second + Delta);
                if (Delta == 0) {
                  MI0.setDesc(TII->get(TargetOpcode::COPY));
                  MI0.getOperand(1).setReg(copy);
                  MI0.removeOperand(2);
                  LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: replaced PTR_ADD with COPY: "; MI0.dump());
                } else {
                  MachineIRBuilder MIB(MI0);
                  Register off = MRI.createGenericVirtualRegister(LLT::scalar(16));
#if 0
                  auto LastLDI = RE.getLastDef();
                  BuildMI(MBB, std::next(LastLDI->getIterator()), LastLDI->getDebugLoc(), TII->get(TargetOpcode::G_CONSTANT), off).addImm(Delta);
#else
                  MachineInstr *CONST = BuildMI(MBB, MI0, MI0.getDebugLoc(), TII->get(TargetOpcode::G_CONSTANT), off).addImm(Delta);
#endif
                  LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: inserted G_CONSTANT: "; CONST->dump());
                  MI0.getOperand(1).setReg(copy);
                  MI0.getOperand(2).setReg(off);
                  LLVM_DEBUG(dbgs() << "Z80EarlyIncrementalLoadingPass: replaced PTR_ADD parameters: "; MI0.dump());
                }
                changes = true;
                applied = true;
                break;
              }
            }
          }
        } while(false);
        if (applied)
          continue;
      }

      if (MI.getOpcode() == TargetOpcode::G_CONSTANT) {
        auto& MO0 = MI.getOperand(0);
        auto& MO1 = MI.getOperand(1);
        Register Dst = MO0.getReg();
        clobber(Dst, false);
        auto& DstRE = getReg(Dst);
        auto Imm = extractImmediate(MO1);
        if (Imm) {
          DstRE.setImm(MI, *Imm);
        }
        continue;
      } else if (MI.getOpcode() == TargetOpcode::G_PTR_ADD) {
        auto& MO0 = MI.getOperand(0);
        auto& MO1 = MI.getOperand(1);
        auto& MO2 = MI.getOperand(2);
        clobber(MO0.getReg(), false);
        auto& DstRE = getReg(MO0.getReg());
        auto& SrcRE = getReg(MO1.getReg());
        auto& OffRE = getReg(MO2.getReg());
        SrcRE.use(MI);
        OffRE.use(MI);
        if (!SrcRE.isSet() && OffRE.isImm()) {
          DstRE.def(MI, SrcRE.getReg(), OffRE.getImm());
        }
        if (SrcRE.isImm() && OffRE.isImm()) {
          DstRE.setImm(MI, SrcRE.getImm() + OffRE.getImm());
        }
        continue;
      } else if (MI.getOpcode() == TargetOpcode::COPY) {
        auto& MO0 = MI.getOperand(0);
        auto& MO1 = MI.getOperand(1);
        Register Dst = MO0.getReg();
        Register Src = MO1.getReg();
        auto& DstRE = getReg(Dst);
        auto& SrcRE = getReg(Src);
        SrcRE.use(MI);
        clobber(Dst, false);
        if (SrcRE.isRegOff() && SrcRE.getRegOff().first != Dst) {
          DstRE.def(MI, SrcRE.getRegOff().first, SrcRE.getRegOff().second);
        } else if (SrcRE.isImm()) {
          DstRE.setImm(MI, SrcRE.getImm());
        } else {
          if (Src.isVirtual()) {
            // try to find cross-branch constants
            if (auto Imm = getIConstantVRegValWithLookThrough(Src, MRI)) {
              int64_t value = Imm->Value.getSExtValue();
              DstRE.setImm(MI, value);
            } else {
              DstRE.def(MI, Src, 0);
            }
          } else {
            DstRE.def(MI, Src, 0);
          }
        }
        continue;
      } else if (MI.getOpcode() == Z80::LDI16) {
        clobber(Z80::BC, false);
        clobber(Z80::HL, false);
        clobber(Z80::DE, false);
        auto& bcRE = getReg(Z80::BC);
        bcRE.dec(MI);
        auto& hlRE = getReg(Z80::HL);
        hlRE.inc(MI);
        auto& deRE = getReg(Z80::DE);
        deRE.inc(MI);
        continue;
      } else if (MI.getOpcode() == Z80::LDD16) {
        clobber(Z80::BC, false);
        clobber(Z80::HL, false);
        clobber(Z80::DE, false);
        auto& bcRE = getReg(Z80::BC);
        bcRE.dec(MI);
        auto& hlRE = getReg(Z80::HL);
        hlRE.dec(MI);
        auto& deRE = getReg(Z80::DE);
        deRE.dec(MI);
        continue;
      } else if (MI.getOpcode() == Z80::CALL16 && isMemcpy(MI.getOperand(0))) {
        clobber(Z80::BC, false);
        clobber(Z80::HL, false);
        clobber(Z80::DE, false);
        auto& bcRE = getReg(Z80::BC);
        auto& hlRE = getReg(Z80::HL);
        auto& deRE = getReg(Z80::DE);
        if (bcRE.isImm()) {
          hlRE.add(MI, bcRE.getImm());
          deRE.add(MI, bcRE.getImm());
        }
        bcRE.setImm(MI, 0);
        continue;
      }
      // XXX also add support for LDIR/LDDR

      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isReg()) {
          continue;
        }
        Register Reg = MO.getReg();
        assert(Reg);
        // Reg gets defined. Clobber anything that uses it.
        if (MO.isDef()) {
          clobber(MO.getReg());
        } else if (MO.isUse() && Regs.count(Reg) != 0) {
            getReg(Reg).use(MI);
        }
      }
    }
  }
  return changes;
}

char Z80EarlyIncrementalLoadingPass::ID = 0;

FunctionPass *llvm::createZ80EarlyIncrementalLoadingPass() {
  return new Z80EarlyIncrementalLoadingPass();
}

static RegisterPass<Z80EarlyIncrementalLoadingPass> X("z80-early-incremental-loading", "Z80 early incremental loading optimization",
                                     false /* Only looks at CFG */,
                                     false /* Analysis Pass */);

