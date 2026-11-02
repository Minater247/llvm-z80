; Page-local updates retain the preceding address, including constant pages.
; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/page-cursor-reuse.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/page-cursor-reuse.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/page-cursor-reuse.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/page-cursor-reuse.py %t.greedy-bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

define void @forward(ptr %source, i8 %start) {
; CHECK-LABEL: name: forward
; CHECK-COUNT-3: INC8r
  %z0 = zext i8 %start to i16
  %a0 = or i16 %z0, 12544
  %d0 = inttoptr i16 %a0 to ptr
  %s0 = getelementptr i8, ptr %source, i16 0
  %v0 = load i8, ptr %s0
  store i8 %v0, ptr %d0
  %b1 = add i8 %start, 1
  %z1 = zext i8 %b1 to i16
  %a1 = or i16 %z1, 12544
  %d1 = inttoptr i16 %a1 to ptr
  %s1 = getelementptr i8, ptr %source, i16 1
  %v1 = load i8, ptr %s1
  store i8 %v1, ptr %d1
  %b2 = add i8 %start, 2
  %z2 = zext i8 %b2 to i16
  %a2 = or i16 %z2, 12544
  %d2 = inttoptr i16 %a2 to ptr
  %s2 = getelementptr i8, ptr %source, i16 2
  %v2 = load i8, ptr %s2
  store i8 %v2, ptr %d2
  %b3 = add i8 %start, 3
  %z3 = zext i8 %b3 to i16
  %a3 = or i16 %z3, 12544
  %d3 = inttoptr i16 %a3 to ptr
  %s3 = getelementptr i8, ptr %source, i16 3
  %v3 = load i8, ptr %s3
  store i8 %v3, ptr %d3
  ret void
}

define void @backward(ptr %source, i8 %start) {
; CHECK-LABEL: name: backward
; CHECK-COUNT-3: DEC8r
  %z0 = zext i8 %start to i16
  %a0 = or i16 %z0, 12544
  %d0 = inttoptr i16 %a0 to ptr
  %s0 = getelementptr i8, ptr %source, i16 0
  %v0 = load i8, ptr %s0
  store i8 %v0, ptr %d0
  %b1 = add i8 %start, -1
  %z1 = zext i8 %b1 to i16
  %a1 = or i16 %z1, 12544
  %d1 = inttoptr i16 %a1 to ptr
  %s1 = getelementptr i8, ptr %source, i16 -1
  %v1 = load i8, ptr %s1
  store i8 %v1, ptr %d1
  %b2 = add i8 %start, -2
  %z2 = zext i8 %b2 to i16
  %a2 = or i16 %z2, 12544
  %d2 = inttoptr i16 %a2 to ptr
  %s2 = getelementptr i8, ptr %source, i16 -2
  %v2 = load i8, ptr %s2
  store i8 %v2, ptr %d2
  %b3 = add i8 %start, -3
  %z3 = zext i8 %b3 to i16
  %a3 = or i16 %z3, 12544
  %d3 = inttoptr i16 %a3 to ptr
  %s3 = getelementptr i8, ptr %source, i16 -3
  %v3 = load i8, ptr %s3
  store i8 %v3, ptr %d3
  ret void
}

define void @ordered(ptr %source, i8 %start) {
; CHECK-LABEL: name: ordered
; CHECK-COUNT-3: INC8r
  %z0 = zext i8 %start to i16
  %a0 = or i16 %z0, 12544
  %d0 = inttoptr i16 %a0 to ptr
  %s0 = getelementptr i8, ptr %source, i16 0
  %v0 = load volatile i8, ptr %s0
  store volatile i8 %v0, ptr %d0
  %b1 = add i8 %start, 1
  %z1 = zext i8 %b1 to i16
  %a1 = or i16 %z1, 12544
  %d1 = inttoptr i16 %a1 to ptr
  %s1 = getelementptr i8, ptr %source, i16 1
  %v1 = load volatile i8, ptr %s1
  store volatile i8 %v1, ptr %d1
  %b2 = add i8 %start, 2
  %z2 = zext i8 %b2 to i16
  %a2 = or i16 %z2, 12544
  %d2 = inttoptr i16 %a2 to ptr
  %s2 = getelementptr i8, ptr %source, i16 2
  %v2 = load volatile i8, ptr %s2
  store volatile i8 %v2, ptr %d2
  %b3 = add i8 %start, 3
  %z3 = zext i8 %b3 to i16
  %a3 = or i16 %z3, 12544
  %d3 = inttoptr i16 %a3 to ptr
  %s3 = getelementptr i8, ptr %source, i16 3
  %v3 = load volatile i8, ptr %s3
  store volatile i8 %v3, ptr %d3
  ret void
}

define void @different_pages(ptr %source, i8 %start) {
; CHECK-LABEL: name: different_pages
  %z0 = zext i8 %start to i16
  %a0 = or i16 %z0, 12544
  %d0 = inttoptr i16 %a0 to ptr
  %s0 = getelementptr i8, ptr %source, i16 0
  %v0 = load volatile i8, ptr %s0
  store volatile i8 %v0, ptr %d0
  %b1 = add i8 %start, 1
  %z1 = zext i8 %b1 to i16
  %a1 = or i16 %z1, 12800
  %d1 = inttoptr i16 %a1 to ptr
  %s1 = getelementptr i8, ptr %source, i16 1
  %v1 = load volatile i8, ptr %s1
  store volatile i8 %v1, ptr %d1
  %b2 = add i8 %start, 2
  %z2 = zext i8 %b2 to i16
  %a2 = or i16 %z2, 12544
  %d2 = inttoptr i16 %a2 to ptr
  %s2 = getelementptr i8, ptr %source, i16 2
  %v2 = load volatile i8, ptr %s2
  store volatile i8 %v2, ptr %d2
  %b3 = add i8 %start, 3
  %z3 = zext i8 %b3 to i16
  %a3 = or i16 %z3, 12800
  %d3 = inttoptr i16 %a3 to ptr
  %s3 = getelementptr i8, ptr %source, i16 3
  %v3 = load volatile i8, ptr %s3
  store volatile i8 %v3, ptr %d3
  ret void
}

; Legalizing masked word arithmetic can create separate equal page constants.
define void @wide_base(ptr %source, i16 %start) {
; CHECK-LABEL: name: wide_base
; CHECK-COUNT-3: INC8r
  %b0 = add i16 %start, 0
  %z0 = and i16 %b0, 255
  %a0 = or i16 %z0, 12544
  %d0 = inttoptr i16 %a0 to ptr
  %s0 = getelementptr i8, ptr %source, i16 0
  %v0 = load i8, ptr %s0
  store i8 %v0, ptr %d0
  %b1 = add i16 %start, 1
  %z1 = and i16 %b1, 255
  %a1 = or i16 %z1, 12544
  %d1 = inttoptr i16 %a1 to ptr
  %s1 = getelementptr i8, ptr %source, i16 1
  %v1 = load i8, ptr %s1
  store i8 %v1, ptr %d1
  %b2 = add i16 %start, 2
  %z2 = and i16 %b2, 255
  %a2 = or i16 %z2, 12544
  %d2 = inttoptr i16 %a2 to ptr
  %s2 = getelementptr i8, ptr %source, i16 2
  %v2 = load i8, ptr %s2
  store i8 %v2, ptr %d2
  %b3 = add i16 %start, 3
  %z3 = and i16 %b3, 255
  %a3 = or i16 %z3, 12544
  %d3 = inttoptr i16 %a3 to ptr
  %s3 = getelementptr i8, ptr %source, i16 3
  %v3 = load i8, ptr %s3
  store i8 %v3, ptr %d3
  ret void
}

; A byte also published to another address keeps its shared calculation.
define void @shared_byte(ptr %source, i8 %start) {
; CHECK-LABEL: name: shared_byte
; CHECK: ADD8ai 2
; CHECK-NOT: INSERT_SUBREG
; CHECK: RET16
  %z0 = zext i8 %start to i16
  %a0 = or i16 %z0, 12544
  %d0 = inttoptr i16 %a0 to ptr
  %v0 = load volatile i8, ptr %source
  store volatile i8 %v0, ptr %d0
  %b1 = add i8 %start, 1
  %z1 = zext i8 %b1 to i16
  %a1 = or i16 %z1, 12544
  %d1 = inttoptr i16 %a1 to ptr
  store volatile i8 %b1, ptr %source
  %v1 = load volatile i8, ptr %source
  store volatile i8 %v1, ptr %d1
  %b2 = add i8 %start, 2
  %z2 = zext i8 %b2 to i16
  %a2 = or i16 %z2, 12544
  %d2 = inttoptr i16 %a2 to ptr
  store volatile i8 %b2, ptr %source
  %v2 = load volatile i8, ptr %source
  store volatile i8 %v2, ptr %d2
  ret void
}
