; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/constant-byte-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/constant-byte-shift.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"
define i8 @shl0(i8 %x) {
  %r = shl i8 %x, 0
  ret i8 %r
}
define i8 @shl1(i8 %x) {
  %r = shl i8 %x, 1
  ret i8 %r
}
define i8 @shl2(i8 %x) {
  %r = shl i8 %x, 2
  ret i8 %r
}
define i8 @shl3(i8 %x) {
  %r = shl i8 %x, 3
  ret i8 %r
}
define i8 @shl4(i8 %x) {
  %r = shl i8 %x, 4
  ret i8 %r
}
define i8 @shl5(i8 %x) {
  %r = shl i8 %x, 5
  ret i8 %r
}
define i8 @shl6(i8 %x) {
  %r = shl i8 %x, 6
  ret i8 %r
}
; CHECK-LABEL: shl7:
; CHECK-NOT: sla
; CHECK: rrc
; CHECK: and{{[ \t]+}}128
; CHECK: ret
define i8 @shl7(i8 %x) {
  %r = shl i8 %x, 7
  ret i8 %r
}
define i8 @lshr0(i8 %x) {
  %r = lshr i8 %x, 0
  ret i8 %r
}
define i8 @lshr1(i8 %x) {
  %r = lshr i8 %x, 1
  ret i8 %r
}
define i8 @lshr2(i8 %x) {
  %r = lshr i8 %x, 2
  ret i8 %r
}
define i8 @lshr3(i8 %x) {
  %r = lshr i8 %x, 3
  ret i8 %r
}
define i8 @lshr4(i8 %x) {
  %r = lshr i8 %x, 4
  ret i8 %r
}
define i8 @lshr5(i8 %x) {
  %r = lshr i8 %x, 5
  ret i8 %r
}
define i8 @lshr6(i8 %x) {
  %r = lshr i8 %x, 6
  ret i8 %r
}
; CHECK-LABEL: lshr7:
; CHECK-NOT: srl
; CHECK: rlc
; CHECK: and{{[ \t]+}}1
; CHECK: ret
define i8 @lshr7(i8 %x) {
  %r = lshr i8 %x, 7
  ret i8 %r
}
define i8 @ashr0(i8 %x) {
  %r = ashr i8 %x, 0
  ret i8 %r
}
define i8 @ashr1(i8 %x) {
  %r = ashr i8 %x, 1
  ret i8 %r
}
define i8 @ashr2(i8 %x) {
  %r = ashr i8 %x, 2
  ret i8 %r
}
define i8 @ashr3(i8 %x) {
  %r = ashr i8 %x, 3
  ret i8 %r
}
define i8 @ashr4(i8 %x) {
  %r = ashr i8 %x, 4
  ret i8 %r
}
define i8 @ashr5(i8 %x) {
  %r = ashr i8 %x, 5
  ret i8 %r
}
define i8 @ashr6(i8 %x) {
  %r = ashr i8 %x, 6
  ret i8 %r
}
define i8 @ashr7(i8 %x) {
  %r = ashr i8 %x, 7
  ret i8 %r
}
