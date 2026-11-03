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

; Code preparation may put the low byte in the pointer and the high byte in
; the integer offset. Both operand orders have the same carry-free address.
define ptr @reversed_address(i8 %page, i8 %index) {
; CHECK-LABEL: reversed_address:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %hi = zext i8 %page to i16
  %offset = shl i16 %hi, 8
  %lo = zext i8 %index to i16
  %base = inttoptr i16 %lo to ptr
  %r = getelementptr i8, ptr %base, i16 %offset
  ret ptr %r
}

define i8 @lookup_reversed(i8 %page, i8 %index) {
; CHECK-LABEL: lookup_reversed:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %hi = zext i8 %page to i16
  %offset = shl i16 %hi, 8
  %lo = zext i8 %index to i16
  %base = inttoptr i16 %lo to ptr
  %r = getelementptr i8, ptr %base, i16 %offset
  %v = load volatile i8, ptr %r
  ret i8 %v
}

define ptr @reversed_unaligned(i16 %page, i16 %index) {
  %hi = and i16 %page, -256
  %offset = or i16 %hi, 7
  %lo = and i16 %index, 255
  %base = inttoptr i16 %lo to ptr
  %r = getelementptr i8, ptr %base, i16 %offset
  ret ptr %r
}

define ptr @reversed_signed(i8 %page, i8 %index) {
  %hi = zext i8 %page to i16
  %offset = shl i16 %hi, 8
  %lo = sext i8 %index to i16
  %base = inttoptr i16 %lo to ptr
  %r = getelementptr i8, ptr %base, i16 %offset
  ret ptr %r
}

define ptr @constant_page(i16 %unused, i16 %index) {
; CHECK-LABEL: constant_page:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %lo = and i16 %index, 255
  %base = inttoptr i16 %lo to ptr
  %r = getelementptr i8, ptr %base, i16 16384
  ret ptr %r
}

; The bounded offset reaches the last byte of this page without carrying.
define ptr @constant_byte_base(i16 %unused, i16 %index) {
; CHECK-LABEL: constant_byte_base:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %lo = and i16 %index, 31
  %r = getelementptr i8, ptr inttoptr (i16 -32 to ptr), i16 %lo
  ret ptr %r
}

; Moving the base by one admits a carry and must preserve full-word arithmetic.
define ptr @constant_carry_base(i16 %unused, i16 %index) {
  %lo = and i16 %index, 31
  %r = getelementptr i8, ptr inttoptr (i16 -31 to ptr), i16 %lo
  ret ptr %r
}

; Both low bytes vary, but their sum still fits in the known page.
define ptr @bounded_byte_base(i16 %base, i16 %index) {
; CHECK-LABEL: bounded_byte_base:
; CHECK-NOT: add{{[ \t]+}}hl,
; CHECK: ret
  %part = and i16 %base, 128
  %address = or i16 %part, 65296
  %p = inttoptr i16 %address to ptr
  %offset = and i16 %index, 111
  %r = getelementptr i8, ptr %p, i16 %offset
  ret ptr %r
}

; An unknown high byte retains the pair operation rather than extending its
; lifetime to feed byte reconstruction.
define ptr @variable_page_base(i16 %base, i16 %index) {
  %address = and i16 %base, -128
  %p = inttoptr i16 %address to ptr
  %offset = and i16 %index, 127
  %r = getelementptr i8, ptr %p, i16 %offset
  ret ptr %r
}

; One extra low bit allows a carry out of page 255, including address wrap.
define ptr @bounded_carry_base(i16 %base, i16 %index) {
  %part = and i16 %base, 128
  %address = or i16 %part, 65297
  %p = inttoptr i16 %address to ptr
  %offset = and i16 %index, 111
  %r = getelementptr i8, ptr %p, i16 %offset
  ret ptr %r
}
