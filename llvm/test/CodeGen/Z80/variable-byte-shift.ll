; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/variable-byte-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/variable-byte-shift.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
target triple = "z80"
define i8 @shift_left(i8 %value, i8 %count) {
  %r = shl i8 %value, %count
  ret i8 %r
}
define i8 @shift_right(i8 %value, i8 %count) {
  %r = lshr i8 %value, %count
  ret i8 %r
}
define i8 @shift_signed(i8 %value, i8 %count) {
  %r = ashr i8 %value, %count
  ret i8 %r
}
