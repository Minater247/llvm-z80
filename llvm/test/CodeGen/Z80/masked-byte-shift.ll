; RUN: %python %S/Inputs/masked-byte-shift.py --emit %t.ll
; RUN: llc -verify-machineinstrs %t.ll -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/masked-byte-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %t.ll -o %t.greedy.s
; RUN: %python %S/Inputs/masked-byte-shift.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %t.ll -o %t.bank.s
; RUN: %python %S/Inputs/masked-byte-shift.py %t.bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %t.ll -o /dev/null
;
; The AND consumes A already. Fold discarded shift bits into accumulator
; rotates while preserving shared results and masks that observe those bits.
; CHECK-LABEL: lshr_1_120:
; CHECK: rrca
; CHECK-NEXT: and 120
; CHECK-NEXT: ret
; CHECK-LABEL: overlap:
; CHECK: srl
; CHECK: srl
; CHECK: srl
; CHECK-NOT: rrca
; CHECK: ret
