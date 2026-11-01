; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/shared-field-address.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: FileCheck %s < %t.greedy.s
; RUN: %python %S/Inputs/shared-field-address.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/shared-field-address.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/shared-field-address.py %t.greedy-bank.s

; Reuse an indexed base for a shared field address. Cover byte wraparound,
; negative and boundary displacements, and preservation of neighboring fields.
target triple = "z80"

; CHECK-LABEL: increment:
; CHECK: inc{{.*}}(iy + 23)
define i8 @increment(ptr %p) nounwind {
  %q = getelementptr i8, ptr %p, i16 3
  %prefix = load i8, ptr %q
  %f = getelementptr i8, ptr %p, i16 23
  %v = load i8, ptr %f
  %n = add i8 %v, 1
  store i8 %n, ptr %f
  %result = xor i8 %n, %prefix
  ret i8 %result
}

; CHECK-LABEL: add_field:
; CHECK: ld{{.*}}(iy + 22)
define i8 @add_field(ptr %p, i8 %delta) nounwind {
  %q = getelementptr i8, ptr %p, i16 3
  %prefix = load i8, ptr %q
  %f = getelementptr i8, ptr %p, i16 22
  %v = load i8, ptr %f
  %n = add i8 %v, %delta
  store i8 %n, ptr %f
  %result = xor i8 %n, %prefix
  ret i8 %result
}

; CHECK-LABEL: negative_field:
; CHECK: ld{{.*}}(iy - 6)
define i8 @negative_field(ptr %p) nounwind {
  %q = getelementptr i8, ptr %p, i16 3
  %prefix = load i8, ptr %q
  %f = getelementptr i8, ptr %p, i16 -6
  %v = load i8, ptr %f
  %n = xor i8 %v, 16
  store i8 %n, ptr %f
  %result = xor i8 %n, %prefix
  ret i8 %result
}

; CHECK-LABEL: last_field:
; CHECK: dec{{.*}}(iy + 127)
define i8 @last_field(ptr %p) nounwind {
  %q = getelementptr i8, ptr %p, i16 3
  %prefix = load i8, ptr %q
  %f = getelementptr i8, ptr %p, i16 127
  %v = load i8, ptr %f
  %n = sub i8 %v, 1
  store i8 %n, ptr %f
  %result = xor i8 %n, %prefix
  ret i8 %result
}

; A single field has no indexed base to reuse. Retain its explicit address.
; CHECK-LABEL: field_only:
; CHECK: inc	(hl)
define i8 @field_only(ptr %p) nounwind {
  %f = getelementptr i8, ptr %p, i16 23
  %v = load i8, ptr %f
  %n = add i8 %v, 1
  store i8 %n, ptr %f
  ret i8 %n
}

; A field outside the displacement range still needs its own pointer.
; CHECK-LABEL: outside_field:
; CHECK: ld{{.*}}128
; CHECK: inc	({{hl|ix|iy}})
define i8 @outside_field(ptr %p) nounwind {
  %q = getelementptr i8, ptr %p, i16 3
  %prefix = load i8, ptr %q
  %f = getelementptr i8, ptr %p, i16 128
  %v = load i8, ptr %f
  %n = add i8 %v, 1
  store i8 %n, ptr %f
  %result = xor i8 %n, %prefix
  ret i8 %result
}
