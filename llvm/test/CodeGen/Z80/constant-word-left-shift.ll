; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/constant-word-left-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/constant-word-left-shift.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/constant-word-left-shift.py %t.bank.s
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o %t.z180.s
; RUN: %python %S/Inputs/constant-word-left-shift.py %t.z180.s

target triple = "z80"

; Whole-byte shifts expose the surviving input byte before allocation.
; CHECK-LABEL: shl15:
; CHECK-NOT: add hl, hl
; CHECK: ret

define i16 @shl0(i16 %x) {
  %r = shl i16 %x, 0
  ret i16 %r
}

define i16 @shl1(i16 %x) {
  %r = shl i16 %x, 1
  ret i16 %r
}

define i16 @shl2(i16 %x) {
  %r = shl i16 %x, 2
  ret i16 %r
}

define i16 @shl3(i16 %x) {
  %r = shl i16 %x, 3
  ret i16 %r
}

define i16 @shl4(i16 %x) {
  %r = shl i16 %x, 4
  ret i16 %r
}

define i16 @shl5(i16 %x) {
  %r = shl i16 %x, 5
  ret i16 %r
}

define i16 @shl6(i16 %x) {
  %r = shl i16 %x, 6
  ret i16 %r
}

define i16 @shl7(i16 %x) {
  %r = shl i16 %x, 7
  ret i16 %r
}

define i16 @shl8(i16 %x) {
  %r = shl i16 %x, 8
  ret i16 %r
}

define i16 @shl9(i16 %x) {
  %r = shl i16 %x, 9
  ret i16 %r
}

define i16 @shl10(i16 %x) {
  %r = shl i16 %x, 10
  ret i16 %r
}

define i16 @shl11(i16 %x) {
  %r = shl i16 %x, 11
  ret i16 %r
}

define i16 @shl12(i16 %x) {
  %r = shl i16 %x, 12
  ret i16 %r
}

define i16 @shl13(i16 %x) {
  %r = shl i16 %x, 13
  ret i16 %r
}

define i16 @shl14(i16 %x) {
  %r = shl i16 %x, 14
  ret i16 %r
}

define i16 @shl15(i16 %x) {
  %r = shl i16 %x, 15
  ret i16 %r
}

define i16 @live_shift(i16 %x, i16 %y) {
  %s = shl i16 %x, 15
  %a = add i16 %s, %y
  %r = xor i16 %a, %x
  ret i16 %r
}
