//=== lib/Target/Z80/Z80RepeatedStoreOptPass.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass detects and transforms repeated absolute-address stores into a
// HL-based sequence, accounting for HL preservation when needed.
//
// Cost model:
// - No save/restore needed: break-even at n ≥ 2 stores (n⋅13 ≥ 10 + n⋅7).
// - With PUSH/POP overhead (21 cycles, 2 bytes): break-even at n ≥ 6 stores (n⋅13 ≥ 31 + n⋅7).
//
//===----------------------------------------------------------------------===//

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

#define DEBUG_TYPE "z80-repeated-store-opt"

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

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  // Determines if an instruction is a store to an absolute address
  bool isAbsoluteAddressStore(const MachineInstr &MI, uint64_t &Address);
  
  // Extract the source register from a store instruction
  Register getStoreSourceRegister(const MachineInstr &MI);
  
  // Check if register HL is live immediately after a specific point in the instruction sequence
  bool isHLLiveAfterPoint(const MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator Point,
                         const LiveRegUnits &LiveRegs);
  
  // Optimize a sequence of repeated stores
  bool optimizeRepeatedStores(MachineBasicBlock &MBB, 
                             MachineBasicBlock::iterator Begin,
                             MachineBasicBlock::iterator End,
                             uint64_t Address,
                             const LiveRegUnits &LiveRegs);
  
  // Helper to optimize subsequences within a block, splitting on HL conflicts
  bool optimizeSubsequences(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator Begin,
                           MachineBasicBlock::iterator End,
                           uint64_t Address,
                           const LiveRegUnits &LiveRegs);
};
} // end anonymous namespace

char Z80RepeatedStoreOptPass::ID = 0;
INITIALIZE_PASS(Z80RepeatedStoreOptPass, DEBUG_TYPE, "Z80 Repeated Store Optimization", false, false)

FunctionPass *llvm::createZ80RepeatedStoreOptPass() {
  return new Z80RepeatedStoreOptPass();
}

void llvm::initializeZ80RepeatedStoreOptPass(PassRegistry &Registry) {
  initializeZ80RepeatedStoreOptPassPass(Registry);
}

// Check if an instruction is a store to an absolute address and extract the address
bool Z80RepeatedStoreOptPass::isAbsoluteAddressStore(const MachineInstr &MI, uint64_t &Address) {
  // Check if this is a store instruction with an immediate address
  switch (MI.getOpcode()) {
  case Z80::LD8ma:   // 8-bit absolute addressing store
  case Z80::LD16ma:  // 16-bit absolute addressing store
  case Z80::LD24ma:  // 24-bit absolute addressing store
  case Z80::LD16mg:  // 16-bit absolute addressing store for G16 registers
  case Z80::LD24mg:  // 24-bit absolute addressing store for G24 registers
  case Z80::LD16mr:  // 16-bit pseudo instruction for memory stores
  case Z80::LD24mr:  // 24-bit pseudo instruction for memory stores
    // Check if the memory operand is an immediate
    if (MI.getOperand(0).isImm()) {
      Address = MI.getOperand(0).getImm();
      return true;
    }
    break;
  }
  return false;
}

// Extract the source register from a store instruction
Register Z80RepeatedStoreOptPass::getStoreSourceRegister(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case Z80::LD8ma:
    // LD8ma always uses register A (implicit in the instruction)
    return Z80::A;
  case Z80::LD16ma:
  case Z80::LD24ma:
  case Z80::LD16mg:
  case Z80::LD24mg:
  case Z80::LD16mr:
  case Z80::LD24mr:
    // These instructions have an explicit source register operand
    if (MI.getNumOperands() >= 2 && MI.getOperand(1).isReg()) {
      return MI.getOperand(1).getReg();
    }
    break;
  }
  // Fallback, should not happen with valid instructions
  return Z80::A;
}

// Check if register HL is live immediately after a specific point in the instruction sequence
bool Z80RepeatedStoreOptPass::isHLLiveAfterPoint(const MachineBasicBlock &MBB,
                                                MachineBasicBlock::iterator Point,
                                                const LiveRegUnits &LiveRegs) {
  const TargetRegisterInfo &TRI = *MBB.getParent()->getSubtarget().getRegisterInfo();
  
  LLVM_DEBUG(dbgs() << "Checking HL liveness immediately after point\n");
  LLVM_DEBUG({
    if (Point == MBB.end()) {
      dbgs() << "Point is end of block\n";
    } else {
      dbgs() << "Point instruction: " << *Point;
    }
  });
  
  // If Point is at the end of the block, check liveness at block exit
  if (Point == MBB.end()) {
    LiveRegUnits LocalLiveRegs(TRI);
    LocalLiveRegs.addLiveOuts(MBB);
    
    MCRegister HLReg = Z80::HL;
    bool hlAvailable = LocalLiveRegs.available(HLReg);
    bool hAvailable = LocalLiveRegs.available(Z80::H);
    bool lAvailable = LocalLiveRegs.available(Z80::L);
    
    LLVM_DEBUG(dbgs() << "At end of block:\n");
    LLVM_DEBUG(dbgs() << "  HL available: " << hlAvailable << "\n");
    LLVM_DEBUG(dbgs() << "  H available: " << hAvailable << "\n");
    LLVM_DEBUG(dbgs() << "  L available: " << lAvailable << "\n");
    LLVM_DEBUG(dbgs() << "  HL is live: " << !hlAvailable << "\n");
    
    return !hlAvailable;
  }
  
  // Create a copy of the current LiveRegs state and step forward through
  // the instructions from Point to see if HL becomes live
  LiveRegUnits LocalLiveRegs = LiveRegs;
  
  LLVM_DEBUG(dbgs() << "Current liveness state before stepping forward:\n");
  LLVM_DEBUG({
    for (unsigned Reg = 1; Reg < TRI.getNumRegs(); ++Reg) {
      if (!LocalLiveRegs.available(Reg)) {
        dbgs() << "  " << TRI.getName(Reg) << "\n";
      }
    }
  });
  
  // Step forward from Point to the end of the block to see when HL becomes live
  for (auto MI = Point; MI != MBB.end(); ++MI) {
    LLVM_DEBUG(dbgs() << "Stepping forward through: " << *MI);
    
    // Check if this instruction makes HL live (i.e., it's used but not defined)
    if (MI->readsRegister(Z80::HL, &TRI) || 
        MI->readsRegister(Z80::H, &TRI) || 
        MI->readsRegister(Z80::L, &TRI)) {
      LLVM_DEBUG(dbgs() << "Found instruction that uses HL: " << *MI << " | HL becomes live after this point\n");
      return true;
    }
    
    // Step forward to update liveness for the next instruction
    LocalLiveRegs.stepForward(*MI);
    
    LLVM_DEBUG(dbgs() << "Live registers after stepping forward:\n");
    LLVM_DEBUG({
      for (unsigned Reg = 1; Reg < TRI.getNumRegs(); ++Reg) {
        if (!LocalLiveRegs.available(Reg)) {
          dbgs() << "  " << TRI.getName(Reg) << "\n";
        }
      }
    });
  }
  
  // If we reach here, check final liveness at the end of the block
  LocalLiveRegs.addLiveOuts(MBB);
  MCRegister HLReg = Z80::HL;
  bool hlAvailable = LocalLiveRegs.available(HLReg);
  LLVM_DEBUG(dbgs() << "At end of forward walk, HL available = " << hlAvailable << "\n");
  return !hlAvailable;
}

// Optimize a sequence of repeated stores
bool Z80RepeatedStoreOptPass::optimizeRepeatedStores(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator Begin,
    MachineBasicBlock::iterator End, uint64_t Address,
    const LiveRegUnits &LiveRegs) {
  
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MBB.getParent()->getSubtarget().getRegisterInfo();
  
  // Count the number of stores in the sequence
  unsigned NumStores = 0;
  for (auto I = Begin; I != End; ++I) {
    uint64_t StoreAddress;
    if (isAbsoluteAddressStore(*I, StoreAddress) && StoreAddress == Address) {
      NumStores++;
    }
  }
  
  // Get HL register
  MCRegister HLReg = Z80::HL;
  
  // Check if HL is live after the last store in this subsequence
  bool IsHLLive = isHLLiveAfterPoint(MBB, End, LiveRegs);
  
  LLVM_DEBUG(dbgs() << "Liveness analysis result: IsHLLive = " << IsHLLive << "\n");
  
  // Check against the cost model
  LLVM_DEBUG(dbgs() << "NumStores: " << NumStores << ", IsHLLive: " << IsHLLive << "\n");
  
  if ((!IsHLLive && NumStores >= 2) || (IsHLLive && NumStores >= 6)) {
    LLVM_DEBUG(dbgs() << "Cost model says optimize\n");
    // Insert LD HL, addr instruction before the first store
    auto InsertPoint = Begin;
    DebugLoc DL = InsertPoint->getDebugLoc();
    
    // If we need to preserve HL, add PUSH HL instruction
    if (IsHLLive) {
      BuildMI(MBB, InsertPoint, DL, TII.get(Z80::PUSH16r))
          .addReg(HLReg);
    }
    
    // Insert LD HL, addr instruction
    BuildMI(MBB, InsertPoint, DL, TII.get(Z80::LD16ri))
        .addReg(HLReg, RegState::Define)
        .addImm(Address);
    
    // Now transform all store instructions in the sequence
    std::vector<std::pair<MachineBasicBlock::iterator, Register>> StoreInstructions;
    
    // Collect all actual store instructions in the range with their source registers
    for (auto I = Begin; I != End; ++I) {
      uint64_t StoreAddress;
      if (isAbsoluteAddressStore(*I, StoreAddress) && StoreAddress == Address) {
        Register SrcReg = getStoreSourceRegister(*I);
        
        // For safety purposes, ensure this store does not use HL as its source.
        if (SrcReg == Z80::HL || SrcReg == Z80::H || SrcReg == Z80::L) {
          LLVM_DEBUG(dbgs() << "Unexpected HL conflict in optimizeRepeatedStores: " 
                           << TRI.getName(SrcReg) << " from instruction: " << *I << "\n");
          return false;
        }
        
        // Validate that an HL-indirect store is actually possible before committing to it
        bool CanOptimize = false;
        switch (I->getOpcode()) {
        case Z80::LD8ma:
          CanOptimize = (SrcReg == Z80::A);
          break;
        case Z80::LD16ma:
        case Z80::LD16mg:
        case Z80::LD16mr:
          // Check if the register is in the R16 class
          CanOptimize = (SrcReg == Z80::HL || SrcReg == Z80::DE || SrcReg == Z80::BC ||
                        SrcReg == Z80::IY || SrcReg == Z80::IX);
          break;
        case Z80::LD24ma:
        case Z80::LD24mg:
        case Z80::LD24mr:
          // Check if the register is in the R24 class
          CanOptimize = (SrcReg == Z80::UHL || SrcReg == Z80::UDE || SrcReg == Z80::UBC ||
                        SrcReg == Z80::UIY || SrcReg == Z80::UIX);
          break;
        }
        
        if (CanOptimize) {
          StoreInstructions.push_back(std::make_pair(I, SrcReg));
        } else {
          LLVM_DEBUG(dbgs() << "Cannot optimize store with register " << SrcReg 
                           << " from instruction: " << *I << "\n");
          return false; // Cannot optimize if any store in the sequence is incompatible
        }
      }
    }
    
    LLVM_DEBUG(dbgs() << "Found " << StoreInstructions.size() << " actual store instructions\n");
    
    // Transform each store instruction
    for (const auto &StoreInfo : StoreInstructions) {
      auto I = StoreInfo.first;
      Register SrcReg = StoreInfo.second;
      
      // Create a new LD (HL), reg instruction to replace the original store
      unsigned NewOpcode = 0;
      
      // Select the appropriate (HL) store opcode based on the size and register type
      switch (I->getOpcode()) {
      case Z80::LD8ma:
        // Use register indirect store through HL: LD (HL), A
        NewOpcode = Z80::LD8pg; // LD (HL), reg
        break;
      case Z80::LD16ma:
      case Z80::LD16mg:
      case Z80::LD16mr:
        NewOpcode = Z80::LD16pr; // 16-bit indirect through register
        break;
      case Z80::LD24ma:
      case Z80::LD24mg:
      case Z80::LD24mr:
        NewOpcode = Z80::LD24pr; // 24-bit indirect through register
        break;
      default:
        // Should not reach here
        LLVM_DEBUG(dbgs() << "Unknown store opcode: " << I->getOpcode() << "\n");
        return false;
      }
      
      // Build the new instruction: LD (HL), SrcReg
      MachineInstrBuilder NewMI = BuildMI(MBB, I, DL, TII.get(NewOpcode))
                                    .addReg(HLReg)   // The register containing the address (HL)
                                    .addReg(SrcReg); // The actual source register
      
      LLVM_DEBUG(dbgs() << "Replacing: " << *I);
      LLVM_DEBUG(dbgs() << "With: " << *NewMI);
      
      // Remove the original instruction
      MBB.erase(I);
    }
    
    // If we preserved HL, restore it with POP HL
    if (IsHLLive) {
      BuildMI(MBB, End, DL, TII.get(Z80::POP16r))
          .addReg(HLReg, RegState::Define);
    }
    
    return true;
  }
  
  return false;
}

// Helper to optimize subsequences within a block, splitting on HL conflicts
bool Z80RepeatedStoreOptPass::optimizeSubsequences(MachineBasicBlock &MBB,
                                                  MachineBasicBlock::iterator Begin,
                                                  MachineBasicBlock::iterator End,
                                                  uint64_t Address,
                                                  const LiveRegUnits &LiveRegs) {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MBB.getParent()->getSubtarget().getRegisterInfo();
  
  // Iterate through the range and split on HL conflicts
  auto SubseqBegin = Begin;
  auto Current = Begin;
  
  while (Current != End) {
    uint64_t StoreAddress;
    if (isAbsoluteAddressStore(*Current, StoreAddress) && StoreAddress == Address) {
      Register SrcReg = getStoreSourceRegister(*Current);
      
      LLVM_DEBUG(dbgs() << "Processing store with source register: " 
                       << TRI.getName(SrcReg) << " in instruction: " << *Current);
      
      // Check if this store uses HL as the source register - this would conflict
      if (SrcReg == Z80::HL || SrcReg == Z80::H || SrcReg == Z80::L) {
        LLVM_DEBUG(dbgs() << "Found HL conflict at store: " << *Current << "\n");
        
        // Optimize the subsequence before the conflict (if it has >= 2 stores)
        if (std::distance(SubseqBegin, Current) >= 2) {
          // Find the last actual store instruction in this subsequence
          auto LastStoreIt = SubseqBegin;
          unsigned StoreCount = 0;
          for (auto I = SubseqBegin; I != Current; ++I) {
            uint64_t StoreAddress;
            if (isAbsoluteAddressStore(*I, StoreAddress) && StoreAddress == Address) {
              LastStoreIt = std::next(I); // Point to instruction after the last store
              StoreCount++;
            }
          }
          
          LLVM_DEBUG(dbgs() << "Optimizing subsequence before HL conflict: " 
                           << StoreCount << " stores in " << std::distance(SubseqBegin, Current) << " instructions\n");
          LLVM_DEBUG(dbgs() << "LastStoreIt points to: ");
          if (LastStoreIt != MBB.end()) {
            LLVM_DEBUG(dbgs() << *LastStoreIt);
          } else {
            LLVM_DEBUG(dbgs() << "end of block\n");
          }
          if (StoreCount >= 2) {  // Only optimize if we have actual stores
            if (optimizeRepeatedStores(MBB, SubseqBegin, LastStoreIt, Address, LiveRegs)) {
              Changed = true;
            }
          }
        }
        
        // Skip the conflicting instruction and start a new subsequence
        ++Current;
        SubseqBegin = Current;
        continue;
      }
    }
    ++Current;
  }
  
  // Optimize the final subsequence (if it has >= 2 stores)
  if (std::distance(SubseqBegin, End) >= 2) {
    // Find the last actual store instruction in this final subsequence
    auto LastStoreIt = SubseqBegin;
    unsigned StoreCount = 0;
    for (auto I = SubseqBegin; I != End; ++I) {
      uint64_t StoreAddress;
      if (isAbsoluteAddressStore(*I, StoreAddress) && StoreAddress == Address) {
        LastStoreIt = std::next(I); // Point to instruction after the last store
        StoreCount++;
      }
    }
    
    LLVM_DEBUG(dbgs() << "Optimizing final subsequence: " 
                     << StoreCount << " stores in " << std::distance(SubseqBegin, End) << " instructions\n");
    if (StoreCount >= 2) {  // Only optimize if we have actual stores
      if (optimizeRepeatedStores(MBB, SubseqBegin, LastStoreIt, Address, LiveRegs)) {
        Changed = true;
      }
    }
  }
  
  return Changed;
}

bool Z80RepeatedStoreOptPass::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Z80 Repeated Store Optimization **********\n"
                    << "********** Function: " << MF.getName() << '\n');
  
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  
  for (auto &MBB : MF) {
    LLVM_DEBUG(dbgs() << "=== Processing Basic Block: " << MBB.getName() << " ===\n");
    
    // Create a LiveRegUnits tracker to check if HL is live
    LiveRegUnits LiveRegs(TRI);
    LiveRegs.addLiveOuts(MBB);
    
    LLVM_DEBUG(dbgs() << "Live-outs at end of block:\n");
    LLVM_DEBUG({
      for (unsigned Reg = 1; Reg < TRI.getNumRegs(); ++Reg) {
        if (!LiveRegs.available(Reg)) {
          dbgs() << "  " << TRI.getName(Reg) << "\n";
        }
      }
    });
    
    // Process the block backwards for liveness analysis
    for (auto MI = MBB.rbegin(); MI != MBB.rend(); ++MI) {
      LLVM_DEBUG(dbgs() << "Stepping backward through: " << *MI);
      LiveRegs.stepBackward(*MI);
      LLVM_DEBUG(dbgs() << "Live registers after stepping backward:\n");
      LLVM_DEBUG({
        for (unsigned Reg = 1; Reg < TRI.getNumRegs(); ++Reg) {
          if (!LiveRegs.available(Reg)) {
            dbgs() << "  " << TRI.getName(Reg) << "\n";
          }
        }
      });
    }
    
    LLVM_DEBUG(dbgs() << "=== Starting forward pass through block ===\n");
    
    // Now process the block forwards to find repeated stores
    for (auto MI = MBB.begin(); MI != MBB.end(); /* increment in loop */) {
      // Get the current instruction and increment iterator
      auto CurrentMI = MI++;
      
      // Check if this is a store to an absolute address
      uint64_t Address;
      if (!isAbsoluteAddressStore(*CurrentMI, Address)) {
        LLVM_DEBUG(dbgs() << "Not an absolute address store: " << *CurrentMI << "\n");
        continue;
      }
      
      LLVM_DEBUG(dbgs() << "Found absolute address store to " << Address << ": " << *CurrentMI << "\n");
      
      // Start of a potential sequence
      auto Begin = CurrentMI;
      auto End = MI; // One past the current instruction
      
      // Look ahead for consecutive stores to the same address
      while (MI != MBB.end()) {
        uint64_t NextAddress;
        
        // Check if this is also a store to the same absolute address
        if (isAbsoluteAddressStore(*MI, NextAddress) && NextAddress == Address) {
          // Found another store to the same address
          End = ++MI;
          continue;
        }
        
        // Check if this instruction uses HL - if so, process current subsequence and break
        if ((MI->isCopy() || MI->getOpcode() == Z80::LD8ri) && 
            (MI->readsRegister(Z80::HL, &TRI) || MI->readsRegister(Z80::H, &TRI) || MI->readsRegister(Z80::L, &TRI))) {
          // Process the current subsequence before the HL conflict
          if (std::distance(Begin, End) >= 2) {
            LLVM_DEBUG(dbgs() << "Found subsequence of " << std::distance(Begin, End) 
                              << " stores to address " << Address << " before HL conflict\n");
            
            if (optimizeSubsequences(MBB, Begin, End, Address, LiveRegs)) {
              LLVM_DEBUG(dbgs() << "Successfully optimized subsequence before HL conflict\n");
              Changed = true;
            }
          }
          
          // Skip past the HL-conflicting instruction
          ++MI;
          // Don't restart sequence building here - let the outer loop handle finding the next store
          break;
        }
        
        // Check if HL is modified between the stores
        if (MI->modifiesRegister(Z80::HL, &TRI) || 
            MI->killsRegister(Z80::HL, &TRI)) {
          break;
        }
        
        // If this instruction doesn't interfere with HL, we can skip over it
        ++MI;
      }
      
      // Check if we have a sequence worth optimizing (at least 2 stores)
      if (std::distance(Begin, End) >= 2) {
        LLVM_DEBUG(dbgs() << "Found sequence of " << std::distance(Begin, End) 
                          << " stores to address " << Address << "\n");
        
        // Optimize this sequence using subsequences (which handles HL conflicts internally)
        if (optimizeSubsequences(MBB, Begin, End, Address, LiveRegs)) {
          LLVM_DEBUG(dbgs() << "Successfully optimized sequence/subsequences\n");
          Changed = true;
        } else {
          LLVM_DEBUG(dbgs() << "Failed to optimize sequence\n");
        }
        
        // Update the iterator to continue after the sequence
        MI = End;
      } else {
        LLVM_DEBUG(dbgs() << "Sequence too short: " << std::distance(Begin, End) << " stores\n");
      }
    }
    
    LLVM_DEBUG(dbgs() << "=== End of Basic Block: " << MBB.getName() << " ===\n");
  }
  
  return Changed;
}
