//===- Z80RegisterAllocator.cpp - Z80 allocation ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Cost-directed allocation for the Z80's overlapping byte, pair and index
// registers. Allocate valuable ranges first; search small groups
// jointly before splitting or spilling. Costs are evaluated on instructions,
// not reduced to a generic coloring objective. SplitEditor and InlineSpiller
// provide liveness maintenance, rematerialization and stack-slot handling.
//
//===----------------------------------------------------------------------===//

#include "../../CodeGen/RegAllocBase.h"
#include "../../CodeGen/SplitKit.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/LiveDebugVariables.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveRegMatrix.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/Spiller.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "z80-regalloc"
STATISTIC(NumReassigned, "Number of Z80 virtual registers reassigned");
STATISTIC(NumExactRegions, "Number of Z80 allocation regions solved exactly");
STATISTIC(NumSplits, "Number of Z80 live ranges split");
STATISTIC(NumSpills, "Number of Z80 live ranges sent to the spiller");

static cl::opt<unsigned>
    SearchBudget("z80-regalloc-search-budget", cl::Hidden, cl::init(65536),
                 cl::desc("Maximum Z80 allocation search states per function"));

static RegisterRegAlloc Z80RegAlloc("z80", "Z80 cost-directed allocator",
                                    createZ80RegisterAllocator);
namespace {
bool isIndex(MCRegister Reg) {
  return Z80::I8RegClass.contains(Reg) || Z80::I16RegClass.contains(Reg) ||
         Z80::I24RegClass.contains(Reg);
}

// Include transfers that have no single instruction. Z80 timings are in
// T-states; index prefixes add four. LEA is only used on eZ80.
unsigned copyCost(MCRegister Dst, MCRegister Src, bool KillSrc,
                  const Z80Subtarget &ST, bool OptSize) {
  if (Dst == Src)
    return 0;
  bool Bytes = OptSize || ST.hasEZ80Ops();
  if (Z80::ALT16RegClass.contains(Dst) || Z80::ALT16RegClass.contains(Src)) {
    if (Z80::ALT16RegClass.contains(Dst, Src))
      return Bytes ? 4 : 16;
    unsigned Prefix = isIndex(Dst) || isIndex(Src);
    return Bytes ? 4 + Prefix : 29 + 4 * Prefix;
  }
  if (Z80::R8RegClass.contains(Dst, Src)) {
    if (Z80::G8RegClass.contains(Dst, Src))
      return Bytes ? 1 : 4;
    if (Z80::I8RegClass.contains(Dst, Src))
      return Z80::X8RegClass.contains(Dst, Src) ||
                     Z80::Y8RegClass.contains(Dst, Src)
                 ? (Bytes ? 2 : 8)
                 : (Bytes ? 6 : 37); // PUSH AF; LD A,src; LD dst,A; POP AF.
    if (Dst == Z80::H || Dst == Z80::L || Src == Z80::H || Src == Z80::L)
      return Bytes ? 4 : 16; // EX DE,HL; LD index-half,byte; EX DE,HL.
    return Bytes ? 2 : 8;
  }

  bool Is24 = Z80::R24RegClass.contains(Dst, Src);
  if (Is24 || Z80::R16RegClass.contains(Dst, Src)) {
    if (Is24 == ST.is24Bit()) {
      MCRegister DE = Is24 ? Z80::UDE : Z80::DE;
      MCRegister HL = Is24 ? Z80::UHL : Z80::HL;
      if (KillSrc && ((Dst == DE && Src == HL) || (Dst == HL && Src == DE)))
        return Bytes ? 1 : 4;
      if (ST.hasEZ80Ops() && isIndex(Src))
        return 3; // LEA dst,index+0.
      unsigned Indices = unsigned(isIndex(Dst)) + unsigned(isIndex(Src));
      if (Is24 || Indices == 2 || (Indices == 1 && OptSize) ||
          (Indices && !ST.hasIndexHalfRegs()))
        return Bytes ? 2 + Indices : 21 + 4 * Indices; // PUSH src; POP dst.
      // The two byte copies share their surrounding EX DE,HL instructions:
      // EX; LD index-low,E; LD index-high,D; EX (or the reverse direction).
      if (Indices == 1 && (Dst == HL || Src == HL))
        return Bytes ? 6 : 24;
    }
    const auto &TRI = *ST.getRegisterInfo();
    return copyCost(TRI.getSubReg(Dst, Z80::sub_low),
                    TRI.getSubReg(Src, Z80::sub_low), false, ST, OptSize) +
           copyCost(TRI.getSubReg(Dst, Z80::sub_high),
                    TRI.getSubReg(Src, Z80::sub_high), false, ST, OptSize);
  }
  // Special registers (F, SP, I, R, MB) have bespoke lowering. Still prefer
  // eliminating the copy, without pretending they are general-purpose bytes.
  return Bytes ? 4 : 16;
}

// Additional access costs beyond the common index prefix. Pseudo loads and
// stores can be much more expensive than transfers between registers: loading
// an index pair from the stack saves HL, exchanges it with the stack, then pops
// the pair. These costs must influence placement before a spill is selected.
unsigned extraAccessCost(const MachineInstr &MI, unsigned Operand, MCRegister P,
                         bool Bytes) {
  unsigned Opc = MI.getOpcode();
  if (Z80::ALT16RegClass.contains(P)) {
    if (Opc == Z80::BANK_LD16ro || Opc == Z80::BANK_LD16or)
      return Bytes ? 6 : 50;
    if ((Opc == Z80::BANK_LD8ap && Operand == 1) ||
        ((Opc == Z80::BANK_LD8pa || Opc == Z80::BANK_LD16ri ||
          Opc == Z80::BANK_INC16 || Opc == Z80::BANK_DEC16) &&
         Operand == 0))
      return Bytes ? 2 : 8;
  }
  if (Opc == Z80::ADD16rr && Operand == 0) {
    if (P == Z80::DE)
      return Bytes ? 2 : 8;
    if (P == Z80::BC)
      return Bytes ? 6 : 37;
  }
  bool LoadPair = Opc == Z80::LD88ro || Opc == Z80::LD88rp;
  bool StorePair = Opc == Z80::LD88or || Opc == Z80::LD88pr;
  unsigned Last = MI.getNumExplicitOperands() - 1;
  if ((LoadPair && Operand == 0) || (StorePair && Operand == Last)) {
    if (Z80::I16RegClass.contains(P))
      return Bytes ? (LoadPair ? 3 : 5) : (LoadPair ? 40 : 61);
  }
  bool LoadByte = Opc == Z80::LD8ro || Opc == Z80::LD8rp;
  bool StoreByte = Opc == Z80::LD8or || Opc == Z80::LD8pr;
  if (((LoadByte && Operand == 0) || (StoreByte && Operand == Last)) &&
      Z80::I8RegClass.contains(P))
    return Bytes ? 3 : 25; // Save AF, transfer through A, restore AF.
  // A zero-offset indexed byte access still encodes a displacement and takes
  // 19 T-states, versus 7 for (HL). The common prefix accounts for four.
  if ((MI.mayLoad() || MI.mayStore()) &&
      (Z80::I16RegClass.contains(P) || Z80::I24RegClass.contains(P)) &&
      !((LoadPair && Operand == 0) || (StorePair && Operand == Last)))
    return Bytes ? 1 : 8;
  return 0;
}

// The range occupying a particular unit must match LiveRegMatrix's lane-aware
// interpretation. Treating the whole interval as live in every byte would
// incorrectly forbid packing independently live high and low subregisters.
const LiveRange *unitRange(const LiveInterval &LI, LaneBitmask Mask) {
  if (!LI.hasSubRanges())
    return &LI;
  for (const auto &SR : LI.subranges())
    if ((SR.LaneMask & Mask).any())
      return &SR;
  return nullptr;
}

bool interferes(const LiveInterval &A, MCRegister RA, const LiveInterval &B,
                MCRegister RB, const TargetRegisterInfo &TRI) {
  if (!TRI.regsOverlap(RA, RB))
    return false;
  if (!A.hasSubRanges() && !B.hasSubRanges())
    return A.overlaps(B);
  for (MCRegUnitMaskIterator UA(RA, &TRI); UA.isValid(); ++UA)
    for (MCRegUnitMaskIterator UB(RB, &TRI); UB.isValid(); ++UB) {
      if ((*UA).first != (*UB).first)
        continue;
      const auto *LA = unitRange(A, (*UA).second);
      const auto *LB = unitRange(B, (*UB).second);
      if (LA && LB && LA->overlaps(*LB))
        return true;
    }
  return false;
}

class Z80RegisterAllocator final : public MachineFunctionPass,
                                   public RegAllocBase,
                                   private LiveRangeEdit::Delegate {
  MachineFunction *MF = nullptr;
  const Z80Subtarget *ST = nullptr;
  MachineBlockFrequencyInfo *MBFI = nullptr;
  std::unique_ptr<Spiller> SpillerInstance;
  std::unique_ptr<SplitAnalysis> SA;
  std::unique_ptr<SplitEditor> SE;
  // A range can be split by block, then by instruction, then must spill.
  DenseMap<Register, unsigned> Stage;
  // Each blocked single-choice range gets one deferred allocation attempt.
  DenseSet<Register> Deferred;
  std::priority_queue<std::tuple<bool, double, Register>> Queue;
  unsigned RemainingStates = 0;
  bool OptSize = false;
  // Transfers depend on target mode, not on the live interval or function.
  // Retain the small lazy table across functions with the same mode. Zero is
  // the uncomputed sentinel; stored costs are biased by one.
  mutable std::array<uint8_t, 2 * Z80::NUM_TARGET_REGS * Z80::NUM_TARGET_REGS>
      CopyCosts{};
  unsigned CopyCostMode = ~0u;

  unsigned transferCost(MCRegister Dst, MCRegister Src, bool KillSrc) const {
    if (Dst == Src)
      return 0;
    unsigned Index = (unsigned(KillSrc) * Z80::NUM_TARGET_REGS + Dst.id()) *
                         Z80::NUM_TARGET_REGS +
                     Src.id();
    uint8_t &Cost = CopyCosts[Index];
    if (!Cost) {
      unsigned Computed = copyCost(Dst, Src, KillSrc, *ST, OptSize);
      assert(Computed < std::numeric_limits<uint8_t>::max());
      Cost = 1 + Computed;
    }
    return Cost - 1;
  }

  bool LRE_CanEraseVirtReg(Register R) override {
    auto &LI = LIS->getInterval(R);
    if (VRM->hasPhys(R)) {
      Matrix->unassign(LI);
      return true;
    }
    LI.clear();
    return false;
  }
  void LRE_WillShrinkVirtReg(Register R) override {
    if (VRM->hasPhys(R)) {
      auto &LI = LIS->getInterval(R);
      Matrix->unassign(LI);
      enqueue(&LI);
    }
  }
  Spiller &spiller() override { return *SpillerInstance; }
  void enqueueImpl(const LiveInterval *LI) override;
  const LiveInterval *dequeue() override {
    if (Queue.empty())
      return nullptr;
    Register R = std::get<2>(Queue.top());
    Queue.pop();
    return &LIS->getInterval(R);
  }
  double frequency(const MachineInstr &MI) const {
    return OptSize ? 1.0
                   : MBFI->getBlockFreqRelativeToEntryBlock(MI.getParent());
  }
  MCRegister resolve(const MachineOperand &MO, MCRegister R) const {
    return MO.getSubReg() ? TRI->getSubReg(R, MO.getSubReg()) : R;
  }
  SmallVector<MCRegister, 12> domain(Register R) const {
    SmallVector<MCRegister, 12> Result;
    for (MCRegister P : RegClassInfo.getOrder(MRI->getRegClass(R))) {
      if (!ST->hasIndexHalfRegs()) {
        if (Z80::I8RegClass.contains(P))
          continue;
        bool UsesHalf = false;
        if (isIndex(P))
          for (const auto &MO : MRI->reg_nodbg_operands(R))
            UsesHalf |=
                MO.getSubReg() && Z80::I8RegClass.contains(resolve(MO, P));
        if (UsesHalf)
          continue;
      }
      Result.push_back(P);
    }
    return Result;
  }
  void
  accessCosts(Register R,
              SmallVectorImpl<std::pair<double, MCRegister>> &Candidates) const;
  SmallVector<Register, 8> blockers(const LiveInterval &LI, MCRegister P);
  bool search(SmallVector<Register, 8> Regs, unsigned Budget,
              Register Pending = Register());
  bool split(const LiveInterval &LI, SmallVectorImpl<Register> &NewRegs,
             bool MainBank = false);
  void refine();
  void coalesceSplits();
  MCRegister selectOrSplit(const LiveInterval &LI,
                           SmallVectorImpl<Register> &NewRegs) override;

public:
  static char ID;
  Z80RegisterAllocator() : MachineFunctionPass(ID) {
    initializeZ80RegisterAllocatorPass(*PassRegistry::getPassRegistry());
  }
  StringRef getPassName() const override { return "Z80 Register Allocator"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoPHIs();
  }
  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().setIsSSA();
  }
  void releaseMemory() override {
    SE.reset();
    SA.reset();
    SpillerInstance.reset();
    Stage.clear();
    Deferred.clear();
    Queue = {};
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
};

// Give untouched ranges an allocation attempt before deferred splits, then
// prioritize spill weight. Dividing by the number of whole-interval
// placements overvalues entry constraints that splitting can remove, evicting
// more expensive loop-carried values. Legal placements are checked once when
// dequeuing; ties use virtual-register numbers for deterministic builds.
void Z80RegisterAllocator::enqueueImpl(const LiveInterval *LI) {
  Queue.emplace(!Deferred.contains(LI->reg()), LI->weight(), LI->reg());
}

// Price every legal placement in one walk over the interval's instructions.
// Neighboring copy domains, operand lists and block weights are invariant
// across candidates; querying them once matters on this small register file.
void Z80RegisterAllocator::accessCosts(
    Register R,
    SmallVectorImpl<std::pair<double, MCRegister>> &Candidates) const {
  bool Bytes = OptSize || ST->hasEZ80Ops();
  for (const MachineInstr &MI : MRI->reg_nodbg_instructions(R)) {
    if (MI.isImplicitDef() || MI.isKill())
      continue;
    double Weight = frequency(MI);
    if (MI.getOpcode() == Z80::BANK_ADD16) {
      const auto &D = MI.getOperand(0), &S = MI.getOperand(2);
      bool IsDst = D.getReg() == R, IsSrc = S.getReg() == R;
      SmallVector<MCRegister, 12> Other;
      if (!IsDst || !IsSrc) {
        Register V = (IsDst ? S : D).getReg();
        if (V.isPhysical())
          Other.push_back(V.asMCReg());
        else if (VRM->hasPhys(V))
          Other.push_back(VRM->getPhys(V));
        else
          Other = domain(V);
      }
      for (auto &[Cost, P] : Candidates) {
        unsigned C = std::numeric_limits<unsigned>::max();
        if (IsDst && IsSrc)
          C = Z80::bankAddCost(P, P, Bytes);
        else
          for (MCRegister O : Other)
            C = std::min(C,
                         Z80::bankAddCost(IsDst ? P : O, IsSrc ? P : O, Bytes));
        if (C != std::numeric_limits<unsigned>::max())
          Cost += Weight * C;
      }
      continue;
    }
    if (MI.isCopy()) {
      const auto &D = MI.getOperand(0), &S = MI.getOperand(1);
      if (S.isUndef())
        continue;
      bool IsDst = D.getReg() == R, IsSrc = S.getReg() == R;
      SmallVector<MCRegister, 12> Other;
      if (!IsDst || !IsSrc) {
        const auto &MO = IsDst ? S : D;
        Register V = MO.getReg();
        if (V.isPhysical())
          Other.push_back(V.asMCReg());
        else if (VRM->hasPhys(V))
          Other.push_back(VRM->getPhys(V));
        else
          Other = domain(V);
        for (auto &P : Other)
          P = resolve(MO, P);
      }
      for (auto &[Cost, P] : Candidates) {
        unsigned C = std::numeric_limits<unsigned>::max();
        if (IsDst && IsSrc)
          C = transferCost(resolve(D, P), resolve(S, P), S.isKill());
        else
          for (MCRegister O : Other) {
            C = std::min(C,
                         transferCost(IsDst ? resolve(D, P) : O,
                                      IsSrc ? resolve(S, P) : O, S.isKill()));
            if (!C)
              break;
          }
        if (C != std::numeric_limits<unsigned>::max())
          Cost += Weight * C;
      }
      continue;
    }
    SmallVector<unsigned, 4> Operands;
    for (unsigned I = 0; I != MI.getNumExplicitOperands(); ++I) {
      const auto &MO = MI.getOperand(I);
      if (MO.isReg() && MO.getReg() == R)
        Operands.push_back(I);
    }
    for (auto &[Cost, P] : Candidates) {
      bool Index = false;
      unsigned Extra = 0;
      for (unsigned I : Operands) {
        MCRegister Resolved = resolve(MI.getOperand(I), P);
        Index |= isIndex(Resolved);
        Extra += extraAccessCost(MI, I, Resolved, Bytes);
      }
      unsigned C = Extra + (Index ? (Bytes ? 1 : 4) : 0);
      if (MI.getOpcode() == Z80::LD8r0 && P != Z80::A)
        C += Bytes ? 1 : 3;
      Cost += Weight * C;
    }
  }
}

SmallVector<Register, 8> Z80RegisterAllocator::blockers(const LiveInterval &LI,
                                                        MCRegister P) {
  SmallVector<Register, 8> Result;
  for (MCRegUnit U : TRI->regunits(P))
    for (const auto *Other : Matrix->query(LI, U).interferingVRegs())
      if (interferes(LI, P, *Other, VRM->getPhys(Other->reg()), *TRI))
        Result.push_back(Other->reg());
  llvm::sort(Result);
  Result.erase(std::unique(Result.begin(), Result.end()), Result.end());
  return Result;
}

// Solve a small allocation region directly. Instruction costs can depend on
// all their operands; interference and shared callee-save costs are separate
// constraints. No pairwise cost-matrix solver or greedy allocation is used.
// Pending, if present, is the new interval being allocated. All other ranges
// have a feasible assignment, restored if the search fails or hits its limit.
bool Z80RegisterAllocator::search(SmallVector<Register, 8> Regs,
                                  unsigned Budget, Register Pending) {
  if (!RemainingStates || Regs.empty() || Regs.size() > 8)
    return false;
  Budget = std::min(Budget, RemainingStates);
  unsigned InitialBudget = Budget;
  struct Node {
    Register R;
    MCRegister Original;
    SmallVector<MCRegister, 12> Domain;
  };
  SmallVector<Node, 8> Nodes;
  SmallPtrSet<const MachineInstr *, 32> Seen;
  SmallVector<const MachineInstr *, 32> Instructions;
  for (Register R : Regs) {
    MCRegister Original = VRM->hasPhys(R) ? VRM->getPhys(R) : MCRegister();
    Nodes.push_back({R, Original, {}});
  }
  // Leave incumbent assignments in the matrix while testing a region. Failed
  // repairs preserve cached queries; only ranges outside this region constrain
  // the domains. Internal conflicts are checked by the search itself.
  auto Available = [&](Register R, MCRegister P) {
    const auto &LI = LIS->getInterval(R);
    if (Matrix->checkRegMaskInterference(LI, P) ||
        Matrix->checkRegUnitInterference(LI, P))
      return false;
    for (MCRegUnitMaskIterator U(P, TRI); U.isValid(); ++U) {
      const auto *Range = unitRange(LI, (*U).second);
      if (!Range)
        continue;
      for (const auto *Other : Matrix->query(*Range, (*U).first).interferingVRegs())
        if (!llvm::is_contained(Regs, Other->reg()))
          return false;
    }
    return true;
  };
  for (auto &N : Nodes) {
    if (N.Original)
      N.Domain.push_back(N.Original);
    for (MCRegister P : domain(N.R))
      if (P != N.Original && Available(N.R, P))
        N.Domain.push_back(P);
  }
  // Propagate forced placements before building the instruction objective.
  // A pinned range can make a repair impossible even when every individual
  // domain is nonempty. Removing those conflicts also exposes further pins.
  SmallVector<unsigned, 8> Pinned;
  for (unsigned I = 0; I != Nodes.size(); ++I)
    if (Nodes[I].Domain.size() == 1)
      Pinned.push_back(I);
  while (!Pinned.empty()) {
    unsigned I = Pinned.pop_back_val();
    if (Nodes[I].Domain.empty())
      continue;
    for (unsigned J = 0; J != Nodes.size(); ++J) {
      if (I == J)
        continue;
      auto &D = Nodes[J].Domain;
      unsigned OldSize = D.size();
      llvm::erase_if(D, [&](MCRegister P) {
        return interferes(LIS->getInterval(Nodes[I].R), Nodes[I].Domain[0],
                          LIS->getInterval(Nodes[J].R), P, *TRI);
      });
      if (OldSize > 1 && D.size() == 1)
        Pinned.push_back(J);
    }
  }
  // No objective construction is needed when a domain is empty, or every
  // allocated range is pinned to its incumbent. Neither case changes the
  // matrix or spends search visits.
  if (llvm::any_of(Nodes, [](const Node &N) { return N.Domain.empty(); }) ||
      (!Pending && llvm::all_of(Nodes, [](const Node &N) {
         return N.Domain.size() == 1;
       })))
    return false;
  for (const auto &N : Nodes)
    if (N.Original)
      Matrix->unassign(LIS->getInterval(N.R));
  for (Register R : Regs)
    for (const MachineInstr &MI : MRI->reg_nodbg_instructions(R))
      if (!MI.isImplicitDef() && !MI.isKill() && Seen.insert(&MI).second)
        Instructions.push_back(&MI);
  // Most constrained first greatly reduces the byte/pair permutation tree.
  llvm::stable_sort(Nodes, [](const Node &A, const Node &B) {
    return A.Domain.size() < B.Domain.size();
  });
  DenseMap<Register, unsigned> Index;
  for (unsigned I = 0; I != Nodes.size(); ++I)
    Index[Nodes[I].R] = I;
  SmallVector<MCRegister, 8> Trial(Nodes.size()), BestRegs(Nodes.size());
  for (unsigned I = 0; I != Nodes.size(); ++I)
    Trial[I] = Nodes[I].Original;

  SmallVector<MCRegister, 8> NewCSRs;
  for (const MCPhysReg *CSR = TRI->getCalleeSavedRegs(MF); *CSR; ++CSR) {
    bool Used = MRI->isPhysRegUsed(*CSR) || Matrix->isPhysRegUsed(*CSR);
    if (!Used)
      NewCSRs.push_back(*CSR);
  }
  struct OperandChoices {
    int NodeIndex = -1;
    SmallVector<MCRegister, 12> Resolved;
    SmallVector<MCRegister, 12> Physical;
  };
  auto Choices = [&](const MachineOperand &MO) {
    OperandChoices O;
    Register R = MO.getReg();
    auto It = Index.find(R);
    if (It != Index.end()) {
      O.NodeIndex = It->second;
      O.Physical = Nodes[It->second].Domain;
    } else if (R.isPhysical())
      O.Physical.push_back(R.asMCReg());
    else if (VRM->hasPhys(R))
      O.Physical.push_back(VRM->getPhys(R));
    else
      O.Physical = domain(R);
    for (MCRegister P : O.Physical)
      O.Resolved.push_back(resolve(MO, P));
    return O;
  };
  // Precompute primitive transfer/access costs once per region. The search
  // still evaluates the full instruction objective (including a shared index
  // prefix), without repeatedly querying register classes or expanding copies.
  SmallVector<unsigned, 8> TrialChoice(Nodes.size());
  for (unsigned I = 0; I != Nodes.size(); ++I)
    if (Trial[I])
      TrialChoice[I] =
          llvm::find(Nodes[I].Domain, Trial[I]) - Nodes[I].Domain.begin() + 1;
  struct BinaryFactor {
    int D, S;
    unsigned Stride;
    SmallVector<double, 16> Costs;
  };
  struct AccessTerm {
    int Node;
    SmallVector<unsigned, 12> Extra;
    SmallVector<bool, 12> Index;
  };
  struct AccessFactor {
    double Weight;
    SmallVector<AccessTerm, 4> Terms;
  };
  SmallVector<BinaryFactor, 16> Binary;
  SmallVector<AccessFactor, 16> Accesses;
  bool Bytes = OptSize || ST->hasEZ80Ops();
  for (const MachineInstr *MI : Instructions) {
    double Weight = frequency(*MI);
    bool BankAdd = MI->getOpcode() == Z80::BANK_ADD16;
    if (MI->isCopy() || BankAdd) {
      const auto &D = MI->getOperand(0), &S = MI->getOperand(BankAdd ? 2 : 1);
      if (S.isUndef())
        continue;
      const auto DC = Choices(D), SC = Choices(S);
      unsigned DN = DC.NodeIndex < 0 ? 1 : DC.Resolved.size() + 1;
      unsigned SN = SC.NodeIndex < 0 ? 1 : SC.Resolved.size() + 1;
      BinaryFactor F{DC.NodeIndex, SC.NodeIndex, SN, {}};
      F.Costs.resize(DN * SN, std::numeric_limits<double>::infinity());
      // Price concrete choices once, then derive partial-assignment bounds
      // from their minima. Re-expanding transfers for row/column zero would
      // repeat the same target cost calculations up to four times.
      for (unsigned DI = DC.NodeIndex < 0 ? 0 : 1; DI != DN; ++DI)
        for (unsigned SI = SC.NodeIndex < 0 ? 0 : 1; SI != SN; ++SI) {
          ArrayRef<MCRegister> DR = DC.Resolved, SR = SC.Resolved;
          if (DI)
            DR = DR.slice(DI - 1, 1);
          if (SI)
            SR = SR.slice(SI - 1, 1);
          double Best = std::numeric_limits<double>::infinity();
          for (MCRegister DP : DR)
            for (MCRegister SP : SR)
              Best = std::min(
                  Best, double(BankAdd ? Z80::bankAddCost(DP, SP, Bytes)
                                       : transferCost(DP, SP, S.isKill())));
          F.Costs[DI * SN + SI] = Weight * Best;
        }
      if (DC.NodeIndex >= 0)
        for (unsigned SI = SC.NodeIndex < 0 ? 0 : 1; SI != SN; ++SI)
          for (unsigned DI = 1; DI != DN; ++DI)
            F.Costs[SI] = std::min(F.Costs[SI], F.Costs[DI * SN + SI]);
      if (SC.NodeIndex >= 0)
        for (unsigned DI = 0; DI != DN; ++DI)
          for (unsigned SI = 1; SI != SN; ++SI)
            F.Costs[DI * SN] =
                std::min(F.Costs[DI * SN], F.Costs[DI * SN + SI]);
      // Constant factors cannot distinguish assignments.
      if (!llvm::all_equal(F.Costs))
        Binary.push_back(std::move(F));
      continue;
    }
    AccessFactor F{Weight, {}};
    for (unsigned I = 0; I != MI->getNumExplicitOperands(); ++I) {
      const auto &MO = MI->getOperand(I);
      if (!MO.isReg() || !MO.getReg())
        continue;
      const auto O = Choices(MO);
      AccessTerm T{O.NodeIndex, {}, {}};
      unsigned Min = std::numeric_limits<unsigned>::max();
      bool AllIndex = !O.Resolved.empty();
      T.Extra.push_back(0);
      T.Index.push_back(false);
      for (MCRegister P : O.Resolved) {
        unsigned C = extraAccessCost(*MI, I, P, Bytes);
        if (I == 0 && MI->getOpcode() == Z80::LD8r0 && P != Z80::A)
          C += Bytes ? 1 : 3;
        Min = std::min(Min, C);
        AllIndex &= isIndex(P);
        if (O.NodeIndex >= 0) {
          T.Extra.push_back(C);
          T.Index.push_back(isIndex(P));
        }
      }
      T.Extra[0] = Min == std::numeric_limits<unsigned>::max() ? 0 : Min;
      T.Index[0] = AllIndex;
      // Remove assignment-independent access costs from the objective.
      if (llvm::all_equal(T.Extra))
        llvm::fill(T.Extra, 0);
      F.Terms.push_back(std::move(T));
    }
    bool FixedPrefix = llvm::any_of(F.Terms, [](const AccessTerm &T) {
      return llvm::all_of(T.Index, [](bool B) { return B; });
    });
    llvm::erase_if(F.Terms, [&](AccessTerm &T) {
      if (FixedPrefix)
        llvm::fill(T.Index, false);
      return llvm::all_of(T.Extra, [](unsigned C) { return C == 0; }) &&
             llvm::none_of(T.Index, [](bool B) { return B; });
    });
    if (!F.Terms.empty())
      Accesses.push_back(std::move(F));
  }
  // Index zero represents an unassigned operand's admissible lower bound.
  // At a leaf each term is exact, and prefixes/saves are charged only once.
  auto Cost = [&]() {
    double Total = 0;
    for (const auto &F : Binary) {
      unsigned D = F.D < 0 ? 0 : TrialChoice[F.D];
      unsigned S = F.S < 0 ? 0 : TrialChoice[F.S];
      Total += F.Costs[D * F.Stride + S];
    }
    for (const auto &F : Accesses) {
      unsigned C = 0;
      bool IndexPrefix = false;
      for (const auto &T : F.Terms) {
        unsigned I = T.Node < 0 ? 0 : TrialChoice[T.Node];
        C += T.Extra[I];
        IndexPrefix |= T.Index[I];
      }
      Total += F.Weight * (C + (IndexPrefix ? (Bytes ? 1 : 4) : 0));
    }
    for (MCRegister CSR : NewCSRs)
      if (llvm::any_of(Trial, [&](MCRegister P) {
            return P && TRI->regsOverlap(P, CSR);
          }))
        Total += Bytes ? 2 + 2 * unsigned(isIndex(CSR))
                       : 21 + 8 * unsigned(isIndex(CSR));
    return Total;
  };
  double Best = Pending ? std::numeric_limits<double>::infinity() : Cost();
  BestRegs = Trial;
  std::fill(Trial.begin(), Trial.end(), MCRegister());
  std::fill(TrialChoice.begin(), TrialChoice.end(), 0);
  bool Improved = false;
  auto Visit = [&](auto &&Self, unsigned Depth) -> bool {
    if (Depth == Nodes.size()) {
      double C = Cost();
      if (C + 1e-6 < Best) {
        Best = C;
        BestRegs = Trial;
        Improved = true;
      }
      return true;
    }
    const auto &N = Nodes[Depth];
    const auto &LI = LIS->getInterval(N.R);
    for (unsigned Choice = 0; Choice != N.Domain.size(); ++Choice) {
      MCRegister P = N.Domain[Choice];
      if (!Budget)
        return false;
      --Budget;
      bool Legal = true;
      for (unsigned J = 0; J != Depth; ++J)
        if (interferes(LI, P, LIS->getInterval(Nodes[J].R), Trial[J], *TRI)) {
          Legal = false;
          break;
        }
      if (!Legal)
        continue;
      Trial[Depth] = P;
      TrialChoice[Depth] = Choice + 1;
      bool Complete = true;
      if (Cost() + 1e-6 < Best)
        Complete = Self(Self, Depth + 1);
      Trial[Depth] = MCRegister();
      TrialChoice[Depth] = 0;
      if (!Complete)
        return false;
    }
    return true;
  };
  if (Cost() + 1e-6 >= Best || Visit(Visit, 0))
    ++NumExactRegions;
  RemainingStates -= InitialBudget - Budget;
  bool Valid = Improved;
  unsigned Applied = 0;
  if (Valid)
    for (unsigned I = 0; I != Nodes.size(); ++I) {
      auto &LI = LIS->getInterval(Nodes[I].R);
      if (Matrix->checkInterference(LI, BestRegs[I]) !=
          LiveRegMatrix::IK_Free) {
        Valid = false;
        break;
      }
      Matrix->assign(LI, BestRegs[I]);
      ++Applied;
    }
  if (!Valid) {
    for (unsigned I = 0; I != Applied; ++I)
      Matrix->unassign(LIS->getInterval(Nodes[I].R));
    for (const auto &N : Nodes)
      if (N.Original)
        Matrix->assign(LIS->getInterval(N.R), N.Original);
  } else {
    for (unsigned I = 0; I != Nodes.size(); ++I)
      NumReassigned += Nodes[I].Original && Nodes[I].Original != BestRegs[I];
  }
  return Valid;
}

bool Z80RegisterAllocator::split(const LiveInterval &LI,
                                 SmallVectorImpl<Register> &NewRegs,
                                 bool MainBank) {
  unsigned S = Stage.lookup(LI.reg());
  if (S >= 2 || !LI.isSpillable())
    return false;
  SA->analyze(&LI);
  if (SA->getUseSlots().size() <= 1)
    return false;
  // Keep groups of accesses in a register on each side of a call. Splitting
  // every index-constrained access separately would reload an expensive pair
  // for every field of a state object, rather than just once after the call.
  SmallVector<std::pair<SlotIndex, SlotIndex>, 8> CallGroups;
  bool AcrossCall = false;
  if (!MainBank && SA->getUseSlots().size() <= 64 &&
      Matrix->checkRegMaskInterference(LI)) {
    const MachineInstr *Previous = nullptr;
    unsigned Scanned = 0;
    for (SlotIndex Use : SA->getUseSlots()) {
      const MachineInstr *MI = LIS->getInstructionFromIndex(Use);
      if (!MI) {
        CallGroups.clear();
        AcrossCall = false;
        break;
      }
      bool Cut = !Previous || Previous->getParent() != MI->getParent();
      if (!Cut && Previous != MI) {
        for (auto I = std::next(Previous->getIterator()); I != MI->getIterator();
             ++I) {
          if (++Scanned > 256)
            break;
          if (llvm::any_of(I->operands(), [](const MachineOperand &MO) {
                return MO.isRegMask();
              })) {
            AcrossCall = true;
            Cut = true;
          }
        }
      }
      if (Scanned > 256) {
        CallGroups.clear();
        AcrossCall = false;
        break;
      }
      if (Cut)
        CallGroups.emplace_back(Use, Use);
      else
        CallGroups.back().second = Use;
      Previous = MI;
    }
  }
  LiveRangeEdit Edit(&LI, NewRegs, *MF, *LIS, VRM, this, &DeadRemats);
  SE->reset(Edit, SplitEditor::SM_Size);
  if (AcrossCall && !MainBank) {
    for (const auto &[First, Last] : CallGroups) {
      SE->openIntv();
      SlotIndex Begin = SE->enterIntvBefore(First);
      SlotIndex End = SE->leaveIntvAfter(Last);
      SE->useIntv(Begin, End);
    }
  } else if (!MainBank && S == 0 && SA->getUseBlocks().size() > 1) {
    for (const auto &BI : SA->getUseBlocks())
      if (SA->shouldSplitSingleBlock(BI, true))
        SE->splitSingleBlock(BI);
  } else {
    // Relax instruction-specific classes: e.g. retain a value in an index
    // register between accesses that require HL. Splitting is bounded by the
    // stage, so the complement and reloads cannot recursively split forever.
    const auto *RC = MRI->getRegClass(LI.reg());
    const auto *Super = TRI->getLargestLegalSuperClass(RC, *MF);
    if (!MainBank && RegClassInfo.getNumAllocatableRegs(Super) <=
                         RegClassInfo.getNumAllocatableRegs(RC))
      return false;
    S = 1;
    SlotIndex First, Last;
    const MachineInstr *Previous = nullptr;
    SmallVector<MCRegister, 8> Legal;
    unsigned GroupSpan = 0, GroupUses = 0;
    auto FinishGroup = [&] {
      if (!First.isValid())
        return;
      SE->openIntv();
      SlotIndex Begin = SE->enterIntvBefore(First);
      SlotIndex End = SE->leaveIntvAfter(Last);
      SE->useIntv(Begin, End);
    };
    for (SlotIndex Use : SA->getUseSlots()) {
      const auto *MI = LIS->getInstructionFromIndex(Use);
      if (!MI || MI->isCopy())
        continue;
      auto *Constraint = MI->getRegClassConstraintEffectForVReg(
          LI.reg(), Super, ST->getInstrInfo(), TRI, true);
      if (MainBank) {
        if (MI->getOpcode() != Z80::BANK_ADD16)
          continue;
        bool Dst = MI->getOperand(0).getReg() == LI.reg();
        Register Other = MI->getOperand(Dst ? 2 : 0).getReg();
        SmallVector<MCRegister, 12> Others;
        if (Other.isPhysical())
          Others.push_back(Other.asMCReg());
        else if (VRM->hasPhys(Other))
          Others.push_back(VRM->getPhys(Other));
        else
          Others = domain(Other);
        bool Bytes = OptSize || ST->hasEZ80Ops();
        unsigned BankCost = ~0u, MainCost = ~0u;
        for (MCRegister P : domain(LI.reg())) {
          unsigned Cost = ~0u;
          for (MCRegister O : Others)
            Cost = std::min(Cost,
                            Z80::bankAddCost(Dst ? P : O, Dst ? O : P, Bytes));
          if (Z80::ALT16RegClass.contains(P))
            BankCost = std::min(BankCost, Cost);
          else if (llvm::none_of(TRI->regunits(P), [&](MCRegUnit U) {
                     if (LIS->getRegUnit(U).overlaps(Use.getBaseIndex(),
                                                     Use.getDeadSlot()))
                       return true;
                     auto At =
                         Matrix->getLiveUnions()[static_cast<unsigned>(U)].find(
                             Use.getBaseIndex());
                     return At.valid() && At.start() < Use.getDeadSlot();
                   }))
            MainCost = std::min(MainCost, Cost);
        }
        unsigned Transfer = (Bytes ? 4 : 29) * (Dst ? 2 : 1);
        if (MainCost == ~0u || BankCost == ~0u ||
            uint64_t(MainCost) + Transfer >= BankCost)
          continue;
        Constraint = TRI->getCommonSubClass(Constraint, &Z80::R16RegClass);
      }

      if (!MainBank && Constraint &&
          RegClassInfo.getNumAllocatableRegs(Constraint) ==
              RegClassInfo.getNumAllocatableRegs(Super))
        continue;
      if (MI == Previous) {
        Last = Use;
        continue;
      }
      SmallVector<MCRegister, 8> Choices;
      if (Constraint)
        for (MCRegister P : RegClassInfo.getOrder(Constraint))
          Choices.push_back(P);
      bool Join = Previous && Previous->getParent() == MI->getParent() &&
                  GroupUses < 4 && GroupSpan < 4;
      unsigned Distance = 1;
      if (Join) {
        for (auto I = std::next(Previous->getIterator());
             I != MI->getIterator(); ++I)
          if (GroupSpan + ++Distance > 4 || I->mayLoadOrStore() ||
              I->isCall() || I->isInlineAsm() ||
              llvm::any_of(I->defs(), [&](const MachineOperand &MO) {
                return MO.isReg() && MO.getReg().isVirtual() &&
                       TRI->getRegSizeInBits(*MRI->getRegClass(MO.getReg())) >
                           8;
              })) {
            Join = false;
            break;
          }
      }
      if (Join) {
        llvm::erase_if(Choices, [&](MCRegister P) {
          if (!llvm::is_contained(Legal, P))
            return true;
          for (MCRegUnit U : TRI->regunits(P))
            if (LIS->getRegUnit(U).overlaps(First.getBaseIndex(),
                                            Use.getDeadSlot()))
              return true;
          return false;
        });
        Join = !Choices.empty();
      }
      if (!Join) {
        FinishGroup();
        First = Use;
        GroupSpan = GroupUses = 0;
        Choices.clear();
        if (Constraint)
          for (MCRegister P : RegClassInfo.getOrder(Constraint))
            Choices.push_back(P);
      }
      Last = Use;
      GroupSpan += Distance;
      ++GroupUses;
      Previous = MI;
      Legal = std::move(Choices);
    }
    FinishGroup();
  }
  if (Edit.empty())
    return false;
  SmallVector<unsigned, 8> Map;
  SE->finish(&Map);
  getAnalysis<LiveDebugVariablesWrapperLegacy>().getLDV().splitRegister(
      LI.reg(), Edit.regs(), *LIS);
  for (unsigned I = 0; I != Edit.size(); ++I) {
    Stage[Edit.get(I)] = Map[I] == 0 ? 2 : S + 1;
    if (MainBank && Map[I] != 0)
      MRI->constrainRegClass(Edit.get(I), &Z80::R16RegClass);
  }
  ++NumSplits;
  return true;
}

MCRegister
Z80RegisterAllocator::selectOrSplit(const LiveInterval &LI,
                                    SmallVectorImpl<Register> &NewRegs) {
  SmallVector<std::pair<double, MCRegister>, 12> Candidates;
  for (MCRegister P : domain(LI.reg()))
    if (!Matrix->checkRegMaskInterference(LI, P) &&
        !Matrix->checkRegUnitInterference(LI, P))
      Candidates.emplace_back(0.0, P);
  // With zero or one legal register there is no placement ranking to price.
  // Repair searches still evaluate the full instruction objective themselves.
  if (Candidates.size() > 1)
    accessCosts(LI.reg(), Candidates);
  llvm::stable_sort(Candidates, [](const auto &A, const auto &B) {
    return A.first < B.first;
  });
  // Try a cheap placement first. Search only where an occupied cheaper register
  // might avoid a costly bank transfer, or where no free register exists.
  MCRegister Free;
  double FreeCost = std::numeric_limits<double>::infinity();
  for (const auto &[Cost, P] : Candidates)
    if (Matrix->checkInterference(LI, P) == LiveRegMatrix::IK_Free) {
      Free = P;
      FreeCost = Cost;
      break;
    }
  unsigned Attempts = 0;
  for (const auto &[Cost, P] : Candidates) {
    if (Cost >= FreeCost || Attempts == 3 || !RemainingStates)
      break;
    auto Region = blockers(LI, P);
    if (Region.size() > 7)
      continue;
    Region.push_back(LI.reg());
    ++Attempts;
    // A free placement supplies an incumbent for the whole region. Moving
    // its blockers is worthwhile only if their increased cost is also paid.
    if (Free)
      Matrix->assign(LI, Free);
    bool Improved = search(std::move(Region), 32, Free ? Register() : LI.reg());
    MCRegister Chosen =
        VRM->hasPhys(LI.reg()) ? VRM->getPhys(LI.reg()) : MCRegister();
    if (Chosen)
      Matrix->unassign(LI);
    if (Improved)
      return Chosen;
  }
  // A free alternate pair can still cost more than entering a primary pair
  // for mixed-bank arithmetic. Split profitable accesses before accepting it.
  if (Free && Z80::ALT16RegClass.contains(Free) && Stage.lookup(LI.reg()) < 2 &&
      llvm::any_of(MRI->reg_nodbg_instructions(LI.reg()),
                   [](const MachineInstr &MI) {
                     return MI.getOpcode() == Z80::BANK_ADD16;
                   }) &&
      split(LI, NewRegs, true))
    return MCRegister();
  if (Free)
    return Free;
  /* With only one legal placement, splitting competes with the range already
     occupying that register. Its tiny, often unspillable fragments can force
     both values into primary pairs before other ranges have been considered,
     leaving one complement on the stack. Give those ranges their allocation
     attempt first. The deferred range still tries placement and repair again,
     and the set bounds deferral to once per original range. */
  if (Candidates.size() == 1 && LI.isSpillable() && Stage.lookup(LI.reg()) == 0 &&
      Deferred.insert(LI.reg()).second) {
    NewRegs.push_back(LI.reg());
    return MCRegister();
  }
  if (split(LI, NewRegs))
    return MCRegister();

  // Compare the aggregate cost of displacing all aliases, not the weight of
  // just one byte in a pair. Each eviction advances the split stage or spills,
  // guaranteeing progress. InlineSpiller rematerializes where
  // possible and folds memory operands using the target's hooks.
  SmallVector<Register, 8> Victims;
  MCRegister SpillReg;
  double Best =
      LI.isSpillable() ? LI.weight() : std::numeric_limits<double>::infinity();
  for (const auto &[Cost, P] : Candidates) {
    auto Conflicts = blockers(LI, P);
    double Weight = 0;
    for (Register R : Conflicts) {
      const auto &Other = LIS->getInterval(R);
      if (!Other.isSpillable()) {
        Weight = std::numeric_limits<double>::infinity();
        break;
      }
      Weight += Other.weight();
    }
    if (Weight < Best) {
      Best = Weight;
      Victims = std::move(Conflicts);
      SpillReg = P;
    }
  }
  if (SpillReg) {
    for (Register R : Victims) {
      if (!VRM->hasPhys(R))
        continue;
      const auto &Other = LIS->getInterval(R);
      Matrix->unassign(Other);
      if (split(Other, NewRegs))
        continue;
      LiveRangeEdit Edit(&Other, NewRegs, *MF, *LIS, VRM, this, &DeadRemats);
      spiller().spill(Edit);
      ++NumSpills;
    }
    return SpillReg;
  }
  if (!LI.isSpillable()) {
    /* A reload can occupy the only index register needed by another reload.
       Move that short range to a different register by displacing spillable
       occupants there. The ordinary repair search keeps outside assignments
       fixed, so it cannot discover this move through an occupied register. */
    for (const auto &[Cost, P] : Candidates) {
      auto Conflicts = blockers(LI, P);
      if (Conflicts.size() != 1)
        continue;
      const auto &Other = LIS->getInterval(Conflicts.front());
      if (Other.isSpillable())
        continue;
      for (MCRegister Q : domain(Other.reg())) {
        if (TRI->regsOverlap(P, Q) ||
            Matrix->checkRegMaskInterference(Other, Q) ||
            Matrix->checkRegUnitInterference(Other, Q))
          continue;
        auto Displaced = blockers(Other, Q);
        if (llvm::any_of(Displaced, [&](Register R) {
              return !LIS->getInterval(R).isSpillable();
            }))
          continue;
        Matrix->unassign(Other);
        for (Register R : Displaced) {
          const auto &Victim = LIS->getInterval(R);
          Matrix->unassign(Victim);
          LiveRangeEdit Edit(&Victim, NewRegs, *MF, *LIS, VRM, this, &DeadRemats);
          spiller().spill(Edit);
          ++NumSpills;
        }
        Matrix->assign(Other, Q);
        return P;
      }
    }
    return ~0u;
  }
  LiveRangeEdit Edit(&LI, NewRegs, *MF, *LIS, VRM, this, &DeadRemats);
  spiller().spill(Edit);
  ++NumSpills;
  return MCRegister();
}

// SplitEditor can leave overlapping copies of the same value at a loop edge.
// Reunite full-register siblings when every overlapping value is connected by
// an identity copy. Ordinary interference checks alone cannot coalesce these.
void Z80RegisterAllocator::coalesceSplits() {
  SmallVector<std::pair<Register, Register>, 16> Copies;
  for (const auto &MBB : *MF)
    for (const auto &MI : MBB)
      if (MI.isCopy() && !MI.getOperand(0).getSubReg() &&
          !MI.getOperand(1).getSubReg() && !MI.getOperand(1).isUndef())
        Copies.emplace_back(MI.getOperand(0).getReg(),
                            MI.getOperand(1).getReg());
  for (auto [D, S] : Copies) {
    if (!D.isVirtual() || !S.isVirtual() || D == S || !VRM->hasPhys(D) ||
        !VRM->hasPhys(S) || VRM->getOriginal(D) != VRM->getOriginal(S))
      continue;
    auto &DL = LIS->getInterval(D), &SL = LIS->getInterval(S);
    if (DL.hasSubRanges() || SL.hasSubRanges())
      continue;
    const auto *RC =
        TRI->getCommonSubClass(MRI->getRegClass(D), MRI->getRegClass(S));
    if (!RC)
      continue;
    DenseMap<const VNInfo *, const VNInfo *> Parent;
    auto Root = [&](const VNInfo *V) {
      while (Parent.count(V) && Parent[V] != V)
        V = Parent[V];
      return V;
    };
    for (const auto &MI : MRI->reg_nodbg_instructions(D)) {
      if (!MI.isCopy() || MI.getOperand(0).getSubReg() ||
          MI.getOperand(1).getSubReg() || MI.getOperand(1).isUndef())
        continue;
      Register To = MI.getOperand(0).getReg(), From = MI.getOperand(1).getReg();
      if (!((To == D && From == S) || (To == S && From == D)))
        continue;
      auto Pos = LIS->getInstructionIndex(MI).getRegSlot();
      auto *DV = LIS->getInterval(To).getVNInfoAt(Pos);
      auto *SV = LIS->getInterval(From).getVNInfoBefore(Pos);
      if (DV && SV)
        Parent[Root(DV)] = Root(SV);
    }
    bool Compatible = true;
    auto A = DL.begin(), B = SL.begin();
    while (A != DL.end() && B != SL.end()) {
      if (A->start < B->end && B->start < A->end &&
          Root(A->valno) != Root(B->valno)) {
        Compatible = false;
        break;
      }
      if (A->end < B->end)
        ++A;
      else
        ++B;
    }
    if (!Compatible)
      continue;
    MCRegister DP = VRM->getPhys(D), SP = VRM->getPhys(S), Chosen;
    Matrix->unassign(DL);
    Matrix->unassign(SL);
    for (MCRegister P : {SP, DP})
      if (RC->contains(P) &&
          Matrix->checkInterference(DL, P) == LiveRegMatrix::IK_Free &&
          Matrix->checkInterference(SL, P) == LiveRegMatrix::IK_Free) {
        Chosen = P;
        break;
      }
    if (!Chosen) {
      Matrix->assign(DL, DP);
      Matrix->assign(SL, SP);
      continue;
    }
    MRI->setRegClass(D, RC);
    MRI->replaceRegWith(S, D);
    SmallVector<MachineInstr *, 8> Dead;
    SmallPtrSet<MachineInstr *, 8> SeenDead;
    for (auto &MI : MRI->reg_nodbg_instructions(D))
      if (MI.isCopy() && MI.getOperand(0).getReg() == D &&
          MI.getOperand(1).getReg() == D && !MI.getOperand(0).getSubReg() &&
          !MI.getOperand(1).getSubReg() && SeenDead.insert(&MI).second)
        Dead.push_back(&MI);
    for (auto *MI : Dead) {
      LIS->RemoveMachineInstrFromMaps(*MI);
      MI->eraseFromParent();
    }
    LIS->removeInterval(D);
    LIS->removeInterval(S);
    MRI->clearKillFlags(D);
    auto &Joined = LIS->createAndComputeVirtRegInterval(D);
    Matrix->invalidateVirtRegs();
    Matrix->assign(Joined, Chosen);
    getAnalysis<LiveDebugVariablesWrapperLegacy>().getLDV().splitRegister(
        S, {D}, *LIS);
  }
}

void Z80RegisterAllocator::refine() {
  // Discover connected regions through actual live-unit overlaps and shared
  // instructions. No quadratic graph or register-choice matrices are built.
  SmallVector<Register, 64> Regs;
  DenseMap<Register, unsigned> Index;
  SmallVector<unsigned, 64> Parent;
  SmallVector<unsigned, 64> Sizes;
  for (unsigned I = 0; I != MRI->getNumVirtRegs(); ++I) {
    Register R = Register::index2VirtReg(I);
    if (VRM->hasPhys(R) && LIS->hasInterval(R) &&
        !LIS->getInterval(R).empty()) {
      Index[R] = Regs.size();
      Parent.push_back(Regs.size());
      Sizes.push_back(1);
      Regs.push_back(R);
    }
  }
  auto Root = [&](unsigned N) {
    while (Parent[N] != N) {
      Parent[N] = Parent[Parent[N]];
      N = Parent[N];
    }
    return N;
  };
  auto Join = [&](Register A, Register B) {
    auto IA = Index.find(A), IB = Index.find(B);
    if (IA == Index.end() || IB == Index.end())
      return;
    unsigned RA = Root(IA->second), RB = Root(IB->second);
    if (RA != RB) {
      Parent[RA] = RB;
      Sizes[RB] += Sizes[RA];
    }
  };
  // Join the cheap instruction edges first. Components only grow: once one
  // exceeds the exact-search limit, its outgoing overlap queries cannot make
  // it eligible again. Small components still query every occupied unit, so
  // their edges into oversized components are retained.
  for (const auto &MBB : *MF)
    for (const auto &MI : MBB) {
      if (MI.isKill() || MI.isImplicitDef() || MI.isDebugInstr())
        continue;
      Register First;
      for (const auto &MO : MI.operands())
        if (MO.isReg() && MO.getReg().isVirtual()) {
          if (First)
            Join(First, MO.getReg());
          First = MO.getReg();
        }
    }
  SmallVector<MCRegUnit, 16> OccupiedUnits;
  for (unsigned U = 0; U != TRI->getNumRegUnits(); ++U)
    if (!Matrix->getLiveUnions()[U].empty())
      OccupiedUnits.push_back(MCRegUnit(U));
  for (unsigned I = 0; I != Regs.size(); ++I) {
    if (Sizes[Root(I)] > 8)
      continue;
    Register R = Regs[I];
    const auto &LI = LIS->getInterval(R);
    for (MCRegUnit U : OccupiedUnits)
      for (const auto *Other : Matrix->query(LI, U).interferingVRegs())
        Join(R, Other->reg());
  }
  SmallVector<SmallVector<Register, 8>, 16> Regions(Regs.size());
  for (unsigned I = 0; I != Regs.size(); ++I)
    Regions[Root(I)].push_back(Regs[I]);
  for (auto &Region : Regions)
    if (!Region.empty() && Region.size() <= 8)
      search(std::move(Region), 16384);
}

void Z80RegisterAllocator::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addRequired<LiveIntervalsWrapperPass>();
  AU.addPreserved<LiveIntervalsWrapperPass>();
  AU.addPreserved<SlotIndexesWrapperPass>();
  AU.addRequired<LiveDebugVariablesWrapperLegacy>();
  AU.addPreserved<LiveDebugVariablesWrapperLegacy>();
  AU.addRequired<LiveStacksWrapperLegacy>();
  AU.addPreserved<LiveStacksWrapperLegacy>();
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
  AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addRequiredID(MachineDominatorsID);
  AU.addPreservedID(MachineDominatorsID);
  AU.addRequired<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addRequired<VirtRegMapWrapperLegacy>();
  AU.addPreserved<VirtRegMapWrapperLegacy>();
  AU.addRequired<LiveRegMatrixWrapperLegacy>();
  AU.addPreserved<LiveRegMatrixWrapperLegacy>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool Z80RegisterAllocator::runOnMachineFunction(MachineFunction &Func) {
  auto Arch = Func.getTarget().getTargetTriple().getArch();
  if (Arch != Triple::z80 && Arch != Triple::ez80)
    reportFatalUsageError("-regalloc=z80 requires a Z80 or eZ80 target");
  MF = &Func;
  ST = &MF->getSubtarget<Z80Subtarget>();
  OptSize = MF->getFunction().hasOptSize();
  unsigned Mode = unsigned(OptSize) | (unsigned(ST->hasEZ80Ops()) << 1) |
                  (unsigned(ST->is24Bit()) << 2) |
                  (unsigned(ST->hasIndexHalfRegs()) << 3);
  if (CopyCostMode != Mode) {
    CopyCosts.fill(0);
    CopyCostMode = Mode;
  }
  RemainingStates = SearchBudget;
  MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  auto &MDT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  auto &Loops = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  RegAllocBase::init(getAnalysis<VirtRegMapWrapperLegacy>().getVRM(),
                     getAnalysis<LiveIntervalsWrapperPass>().getLIS(),
                     getAnalysis<LiveRegMatrixWrapperLegacy>().getLRM());
  VirtRegAuxInfo VRAI(*MF, *LIS, *VRM, Loops, *MBFI,
                      &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI());
  VRAI.calculateSpillWeightsAndHints();
  SpillerInstance.reset(createInlineSpiller(
      {*LIS, getAnalysis<LiveStacksWrapperLegacy>().getLS(), MDT, *MBFI}, *MF,
      *VRM, VRAI));
  SA = std::make_unique<SplitAnalysis>(*VRM, *LIS, Loops);
  SE = std::make_unique<SplitEditor>(*SA, *LIS, *VRM, MDT, *MBFI, VRAI);
  allocatePhysRegs();
  postOptimization();
  coalesceSplits();
  if (RemainingStates)
    refine();
  releaseMemory();
  return true;
}
} // namespace

char Z80RegisterAllocator::ID = 0;
INITIALIZE_PASS_BEGIN(Z80RegisterAllocator, DEBUG_TYPE,
                      "Z80 Register Allocator", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveDebugVariablesWrapperLegacy)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LiveStacksWrapperLegacy)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(VirtRegMapWrapperLegacy)
INITIALIZE_PASS_DEPENDENCY(LiveRegMatrixWrapperLegacy)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(Z80RegisterAllocator, DEBUG_TYPE, "Z80 Register Allocator",
                    false, false)
FunctionPass *llvm::createZ80RegisterAllocator() {
  return new Z80RegisterAllocator();
}
