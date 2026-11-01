; RUN: %python %S/Inputs/shift-ez80.py --emit %t.ll
; RUN: llc -verify-machineinstrs %t.ll -o %t.s
; RUN: %python %S/Inputs/shift-ez80.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %t.ll -o %t.greedy.s
; RUN: %python %S/Inputs/shift-ez80.py %t.greedy.s
;
; A native 24-bit word splits into three bytes. Binary splitting would create
; unsupported 12-bit and 6-bit intermediates for right shifts.
