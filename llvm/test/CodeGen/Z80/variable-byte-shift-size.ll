; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/variable-byte-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/variable-byte-shift.py %t.greedy.s
; RUN: llc -regalloc=z80 -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/variable-byte-shift.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/variable-byte-shift.py %t.greedy-bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: FileCheck %s < %t.s
;
; Size optimization keeps variable shifts as counted byte loops. Every value
; and defined count is executed, including zero and negative signed inputs.
; CHECK-LABEL: shift_left:
; CHECK: inc
; CHECK: dec
; CHECK: add a, a
; CHECK-LABEL: shift_right:
; CHECK: inc
; CHECK: dec
; CHECK: srl
; CHECK-LABEL: shift_signed:
; CHECK: inc
; CHECK: dec
; CHECK: sra
target triple = "z80"
define i8 @shift_left(i8 %value, i8 %count) minsize {
  %r = shl i8 %value, %count
  ret i8 %r
}
define i8 @shift_right(i8 %value, i8 %count) minsize {
  %r = lshr i8 %value, %count
  ret i8 %r
}
define i8 @shift_signed(i8 %value, i8 %count) minsize {
  %r = ashr i8 %value, %count
  ret i8 %r
}
