; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/page-offset.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/page-offset.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/page-offset.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/page-offset.py %t.greedy-bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

define ptr @page_address(i8 %page, i8 %index) {
; CHECK-LABEL: page_address:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %hi = zext i8 %page to i16
  %base = shl i16 %hi, 8
  %p = inttoptr i16 %base to ptr
  %lo = zext i8 %index to i16
  %r = getelementptr i8, ptr %p, i16 %lo
  ret ptr %r
}

define ptr @masked_address(i16 %base, i16 %index) {
; CHECK-LABEL: masked_address:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %hi = and i16 %base, -256
  %p = inttoptr i16 %hi to ptr
  %lo = and i16 %index, 255
  %r = getelementptr i8, ptr %p, i16 %lo
  ret ptr %r
}

define ptr @unaligned(i16 %base, i16 %index) {
  %hi = and i16 %base, -256
  %bits = or i16 %hi, 7
  %p = inttoptr i16 %bits to ptr
  %lo = and i16 %index, 255
  %r = getelementptr i8, ptr %p, i16 %lo
  ret ptr %r
}

define ptr @wide_offset(i16 %base, i16 %index) {
  %hi = and i16 %base, -256
  %p = inttoptr i16 %hi to ptr
  %r = getelementptr i8, ptr %p, i16 %index
  ret ptr %r
}

define ptr @signed_offset(i8 %page, i8 %index) {
  %hi = zext i8 %page to i16
  %base = shl i16 %hi, 8
  %p = inttoptr i16 %base to ptr
  %lo = sext i8 %index to i16
  %r = getelementptr i8, ptr %p, i16 %lo
  ret ptr %r
}

define i8 @lookup(i8 %page, i8 %index) {
; CHECK-LABEL: lookup:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %hi = zext i8 %page to i16
  %base = shl i16 %hi, 8
  %p = inttoptr i16 %base to ptr
  %lo = zext i8 %index to i16
  %r = getelementptr i8, ptr %p, i16 %lo
  %value = load volatile i8, ptr %r
  ret i8 %value
}

define ptr @constant_offset(i16 %base) {
  %hi = and i16 %base, -256
  %p = inttoptr i16 %hi to ptr
  %r = getelementptr i8, ptr %p, i16 1
  ret ptr %r
}
