; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/word-right-shift.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/word-right-shift.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/word-right-shift.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/word-right-shift.py %t.greedy-bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

define i16 @lshr1(i16 %x) {
; CHECK-LABEL: lshr1:
; CHECK: srl
; CHECK-NEXT: rr
  %r = lshr i16 %x, 1
  ret i16 %r
}

define i16 @lshr2(i16 %x) {
; CHECK-LABEL: lshr2:
; CHECK: srl
; CHECK-NEXT: rr
; CHECK: srl
; CHECK-NEXT: rr
  %r = lshr i16 %x, 2
  ret i16 %r
}

define i16 @lshr3(i16 %x) {
; CHECK-LABEL: lshr3:
; CHECK: srl
; CHECK-NEXT: rr
; CHECK: srl
; CHECK-NEXT: rr
; CHECK: srl
; CHECK-NEXT: rr
  %r = lshr i16 %x, 3
  ret i16 %r
}

define i16 @lshr4(i16 %x) {
; CHECK-LABEL: lshr4:
; CHECK: srl
; CHECK-NEXT: rr
; CHECK: srl
; CHECK-NEXT: rr
; CHECK: srl
; CHECK-NEXT: rr
; CHECK: srl
; CHECK-NEXT: rr
  %r = lshr i16 %x, 4
  ret i16 %r
}

define i16 @lshr5(i16 %x) {
  %r = lshr i16 %x, 5
  ret i16 %r
}

define i16 @lshr7(i16 %x) {
  %r = lshr i16 %x, 7
  ret i16 %r
}

define i16 @lshr8(i16 %x) {
  %r = lshr i16 %x, 8
  ret i16 %r
}

define i16 @lshr15(i16 %x) {
  %r = lshr i16 %x, 15
  ret i16 %r
}

define i16 @ashr1(i16 %x) {
; CHECK-LABEL: ashr1:
; CHECK: sra
; CHECK-NEXT: rr
  %r = ashr i16 %x, 1
  ret i16 %r
}

define i16 @ashr2(i16 %x) {
; CHECK-LABEL: ashr2:
; CHECK: sra
; CHECK-NEXT: rr
; CHECK: sra
; CHECK-NEXT: rr
  %r = ashr i16 %x, 2
  ret i16 %r
}

define i16 @ashr3(i16 %x) {
; CHECK-LABEL: ashr3:
; CHECK: sra
; CHECK-NEXT: rr
; CHECK: sra
; CHECK-NEXT: rr
; CHECK: sra
; CHECK-NEXT: rr
  %r = ashr i16 %x, 3
  ret i16 %r
}

define i16 @ashr4(i16 %x) {
; CHECK-LABEL: ashr4:
; CHECK: sra
; CHECK-NEXT: rr
; CHECK: sra
; CHECK-NEXT: rr
; CHECK: sra
; CHECK-NEXT: rr
; CHECK: sra
; CHECK-NEXT: rr
  %r = ashr i16 %x, 4
  ret i16 %r
}

define i16 @ashr5(i16 %x) {
  %r = ashr i16 %x, 5
  ret i16 %r
}

define i16 @ashr7(i16 %x) {
  %r = ashr i16 %x, 7
  ret i16 %r
}

define i16 @ashr8(i16 %x) {
  %r = ashr i16 %x, 8
  ret i16 %r
}

define i16 @ashr15(i16 %x) {
  %r = ashr i16 %x, 15
  ret i16 %r
}

define i16 @live_shifts(i16 %x, i16 %y) {
  %a = lshr i16 %x, 3
  %b = ashr i16 %y, 4
  %c = add i16 %a, %b
  %d = xor i16 %c, %x
  ret i16 %d
}
define i16 @narrow_input(i8 %x) {
; CHECK-LABEL: narrow_input:
; CHECK: rlca
; CHECK-NEXT: rlca
; CHECK-NEXT: rlca
; CHECK-NEXT: rlca
; CHECK-NEXT: and{{[ \t]+}}15
; CHECK-NOT: srl
; CHECK: ret
  %a = zext i8 %x to i16
  %r = lshr i16 %a, 4
  ret i16 %r
}

define i16 @narrow_load(ptr %p) {
; CHECK-LABEL: narrow_load:
; CHECK: rlca
; CHECK-NEXT: rlca
; CHECK-NEXT: rlca
; CHECK-NEXT: rlca
; CHECK-NEXT: and{{[ \t]+}}15
; CHECK-NOT: srl
; CHECK: ret
  %x = load volatile i8, ptr %p
  %a = zext i8 %x to i16
  %r = lshr i16 %a, 4
  ret i16 %r
}
