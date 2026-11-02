; RUN: %python %S/Inputs/masked-word-shift.py --emit %t.ll
; RUN: llc -verify-machineinstrs %t.ll -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/masked-word-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %t.ll -o %t.greedy.s
; RUN: %python %S/Inputs/masked-word-shift.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %t.ll -o %t.bank.s
; RUN: %python %S/Inputs/masked-word-shift.py %t.bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %t.ll -o /dev/null
;
; Extracting bit seven needs only the low byte, even when the word result
; subsequently contributes to an address. Shared and cross-byte shifts retain
; every observed bit.
; CHECK-LABEL: w16_lshr_6_2:
; CHECK-NOT: ld a, d
; CHECK: rlca
; CHECK-NEXT: rlca
; CHECK-NEXT: and 2
; CHECK: ret
; CHECK-LABEL: w16_lshr_7_128:
; CHECK: ld a, d
; CHECK: rlca
; CHECK: and -128
; CHECK: ret
