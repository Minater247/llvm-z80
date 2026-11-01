; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/constant-byte-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/constant-byte-shift.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/constant-byte-shift.py %t.bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"
define i8 @shl0(i8 %x) nounwind {
  %r = shl i8 %x, 0
  ret i8 %r
}
define i8 @shl1(i8 %x) nounwind {
  %r = shl i8 %x, 1
  ret i8 %r
}
define i8 @shl2(i8 %x) nounwind {
  %r = shl i8 %x, 2
  ret i8 %r
}
define i8 @shl3(i8 %x) nounwind {
  %r = shl i8 %x, 3
  ret i8 %r
}
define i8 @shl4(i8 %x) nounwind {
  %r = shl i8 %x, 4
  ret i8 %r
}
define i8 @shl5(i8 %x) nounwind {
  %r = shl i8 %x, 5
  ret i8 %r
}
define i8 @shl6(i8 %x) nounwind {
  %r = shl i8 %x, 6
  ret i8 %r
}
; CHECK-LABEL: shl7:
; CHECK-NOT: sla
; CHECK: rrc
; CHECK: and{{[ \t]+}}128
; CHECK: ret
define i8 @shl7(i8 %x) nounwind {
  %r = shl i8 %x, 7
  ret i8 %r
}
define i8 @lshr0(i8 %x) nounwind {
  %r = lshr i8 %x, 0
  ret i8 %r
}
define i8 @lshr1(i8 %x) nounwind {
  %r = lshr i8 %x, 1
  ret i8 %r
}
define i8 @lshr2(i8 %x) nounwind {
  %r = lshr i8 %x, 2
  ret i8 %r
}
define i8 @lshr3(i8 %x) nounwind {
  %r = lshr i8 %x, 3
  ret i8 %r
}
; CHECK-LABEL: lshr4:
; CHECK-COUNT-4: rlca
; CHECK: and{{[ \t]+}}15
define i8 @lshr4(i8 %x) nounwind {
  %r = lshr i8 %x, 4
  ret i8 %r
}
define i8 @lshr5(i8 %x) nounwind {
  %r = lshr i8 %x, 5
  ret i8 %r
}
define i8 @lshr6(i8 %x) nounwind {
  %r = lshr i8 %x, 6
  ret i8 %r
}
; CHECK-LABEL: lshr7:
; CHECK-NOT: srl
; CHECK: rlc
; CHECK: and{{[ \t]+}}1
; CHECK: ret
define i8 @lshr7(i8 %x) nounwind {
  %r = lshr i8 %x, 7
  ret i8 %r
}
define i8 @ashr0(i8 %x) nounwind {
  %r = ashr i8 %x, 0
  ret i8 %r
}
define i8 @ashr1(i8 %x) nounwind {
  %r = ashr i8 %x, 1
  ret i8 %r
}
define i8 @ashr2(i8 %x) nounwind {
  %r = ashr i8 %x, 2
  ret i8 %r
}
define i8 @ashr3(i8 %x) nounwind {
  %r = ashr i8 %x, 3
  ret i8 %r
}
define i8 @ashr4(i8 %x) nounwind {
  %r = ashr i8 %x, 4
  ret i8 %r
}
define i8 @ashr5(i8 %x) nounwind {
  %r = ashr i8 %x, 5
  ret i8 %r
}
define i8 @ashr6(i8 %x) nounwind {
  %r = ashr i8 %x, 6
  ret i8 %r
}
define i8 @ashr7(i8 %x) nounwind {
  %r = ashr i8 %x, 7
  ret i8 %r
}

; Keep the original byte and a second operand alive across accumulator shifts.
define i8 @live_shifts(i8 %x, i8 %y) nounwind {
  %low = lshr i8 %x, 4
  %high = lshr i8 %y, 5
  %joined = or i8 %low, %high
  %result = xor i8 %joined, %x
  ret i8 %result
}

; Share the first two shifts when extracting overlapping bit fields.
; CHECK-LABEL: shared_shift:
; CHECK-NOT: rlca
; CHECK: srl
; CHECK-NOT: rlca
; CHECK: ret
define i8 @shared_shift(i8 %x) nounwind {
  %small = lshr i8 %x, 2
  %large = lshr i8 %x, 4
  %result = xor i8 %small, %large
  ret i8 %result
}
