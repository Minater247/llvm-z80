//=- Z80.td - Define Z80 Combine Rules -----------------------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

def combine_trunc_shift_matchdata : GIDefMatchData<"Register">;
def combine_trunc_shift : GICombineRule<
  (defs root:$mi, combine_trunc_shift_matchdata:$matchinfo),
  (match (wip_match_opcode G_TRUNC):$mi,
         [{ return matchCombineTruncShift(*${mi}, MRI, ${matchinfo}); }]),
  (apply [{ applyCombineTruncShift(*${mi}, B, Observer, ${matchinfo}); }])>;

def flip_setcc_matchdata : GIDefMatchData<"MachineInstr *">;
def flip_setcc_cond : GICombineRule<
  (defs root:$mi, flip_setcc_matchdata:$matchinfo),
  (match (wip_match_opcode G_XOR):$mi,
         [{ return matchFlipSetCCCond(*${mi}, MRI, ${matchinfo}); }]),
  (apply [{ applyFlipSetCCCond(*${mi}, B, Observer, *${matchinfo}); }])>;

def combine_load_store : GICombineRule<
  (defs root:$mi),
  (match (wip_match_opcode G_LOAD):$mi,
         [{ return matchCombineLoadStore(*${mi}, MRI); }]),
  (apply [{ applyCombineLoadStore(*${mi}, MRI, B, Observer); }])>;

def Z80PreLegalizerCombiner
  : GICombiner<"Z80PreLegalizerCombinerImpl",
               [all_combines, combine_trunc_shift, flip_setcc_cond,
                combine_load_store]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

def Z80PostLegalizerCombiner
  : GICombiner<"Z80PostLegalizerCombinerImpl", [all_combines]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}
