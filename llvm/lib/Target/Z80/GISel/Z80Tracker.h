
#ifndef LLVM_LIB_TARGET_Z80_Z80TRACKER_H
#define LLVM_LIB_TARGET_Z80_Z80TRACKER_H

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

namespace llvm {
  class Z80Tracker {
    static constexpr const char *DEBUG_TYPE = "z80-tracker";

  public:
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
              LLVM_DEBUG(dbgs() << "Z80Tracker: clearing kill for " << getName() << " from: "; lastUse->dump());
            }
          }
        } else if (lastDef) {
          for (auto& MO : lastDef->operands()) {
            if (MO.isReg() && MO.getReg() == reg && MO.isDead()) {
              MO.setIsDead(false);
              LLVM_DEBUG(dbgs() << "Z80Tracker: clearing kill for " << getName() << " from: "; lastDef->dump());
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
        LLVM_DEBUG(dbgs() << "Z80Tracker: built virtual copy: "; COPY->dump());
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
          LLVM_DEBUG(dbgs() << "Z80Tracker: clearing " << getName() << "\n");
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
        LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << getName(Reg) << " + " << offset << "\n");
        regOff.emplace(Reg, offset);
        immediate.reset();
        _def(MI);
      }

      void setImm(MachineInstr& MI, int64_t i) {
        LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << i << "\n");
        regOff.reset();
        immediate = i;
        _def(MI);
      }

      void inc(MachineInstr& MI) {
        if (immediate) {
          *immediate = *immediate + 1;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << *immediate << "\n");
        } else if (regOff) {
          regOff->second = regOff->second + 1;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
        }
        _def(MI);
      }

      void add(MachineInstr& MI, int64_t value) {
        if (immediate) {
          *immediate = *immediate + value;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << *immediate << "\n");
        } else if (regOff) {
          regOff->second = regOff->second + value;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
        }
        _def(MI);
      }

      void sub(MachineInstr& MI, int64_t value) {
        if (immediate) {
          *immediate = *immediate - value;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << *immediate << "\n");
        } else if (regOff) {
          regOff->second = regOff->second - value;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
        }
        _def(MI);
      }

      void dec(MachineInstr& MI) {
        if (immediate) {
          *immediate = *immediate - 1;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << *immediate << "\n");
        } else if (regOff) {
          regOff->second = regOff->second - 1;
          LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " = " << getName(regOff->first) << " + " << regOff->second << "\n");
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
        LLVM_DEBUG(dbgs() << "Z80Tracker: " << getName(reg) << " used\n");
      }

      void clearLastKill() {
        assert(lastUse);
        for (auto& MO : lastUse->operands()) {
          if (MO.isReg() && MO.getReg() == reg && MO.isKill()) {
            MO.setIsKill(false);
            LLVM_DEBUG(dbgs() << "Z80Tracker: cleared kill in "; lastUse->dump());
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

  private:
    std::map<Register, RegisterEntry> Regs;
    MachineBasicBlock& MBB;
    const TargetRegisterInfo *TRI;
    const TargetInstrInfo *TII;
    MachineRegisterInfo& MRI;

  public:
    RegisterEntry& getReg(Register Reg) {
      assert(Reg);
      auto fit = Regs.find(Reg);
      if (fit != Regs.end())
          return fit->second;
      return Regs.emplace(Reg, RegisterEntry(TRI, Reg)).first->second;
    }

    std::map<Register, RegisterEntry>& getRegs() { return Regs; }

    void clobber(Register Reg, bool ClobberSelf = true) {
      assert(Reg);
      for (auto& e : Regs) {
        if (!ClobberSelf && e.first == Reg) {
          continue;
        }
        if (e.second.isRelated(Reg)) {
          e.second.reset();
        }
      }
    }

    Optional<int64_t> extractImmediate(MachineOperand& MO);

    static bool isMemcpy(const MachineOperand& Op);

    Z80Tracker(MachineBasicBlock& MBB);

    void process(MachineInstr& MI);
  };
} // namespace llvm

#endif // LLVM_LIB_TARGET_Z80_Z80TRACKER_H

