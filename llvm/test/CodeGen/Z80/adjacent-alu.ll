; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/adjacent-alu.py llc %s %t.dir
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
;
; Retain the accumulated value in A while consuming a neighboring byte.
; The base remains available for a later access, including page boundaries.
target triple = "z80"

define i8 @run(ptr %p, i8 %initial) nounwind {
; CHECK-LABEL: name: run
; CHECK: ADD8ar
; CHECK: ALU8_ADJ
; CHECK: LD8
  %lo = load i8, ptr %p
  %a = add i8 %lo, %initial
  store volatile i8 %a, ptr inttoptr (i16 8960 to ptr)
  %q = getelementptr i8, ptr %p, i16 1
  %hi = load i8, ptr %q
  %value = add i8 %hi, %a
  store volatile i8 %value, ptr inttoptr (i16 8961 to ptr)
  %again = load volatile i8, ptr %p
  %result = xor i8 %value, %again
  ret i8 %result
}

define i8 @ordered(ptr %p, i8 %initial) nounwind {
; CHECK-LABEL: name: ordered
; CHECK-NOT: ALU8_ADJ
  %a = load volatile i8, ptr %p
  %q = getelementptr i8, ptr %p, i16 1
  %b = load volatile i8, ptr %q
  %sum = add i8 %a, %initial
  %result = add i8 %sum, %b
  ret i8 %result
}

define i8 @store_boundary(ptr %p, i8 %initial) nounwind {
; CHECK-LABEL: name: store_boundary
; CHECK-NOT: ALU8_ADJ
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i16 1
  %b = load i8, ptr %q
  store volatile i8 %initial, ptr %q
  %sum = add i8 %a, %initial
  %result = add i8 %sum, %b
  ret i8 %result
}

define i8 @shared_load(ptr %p, i8 %initial) nounwind {
; CHECK-LABEL: name: shared_load
; CHECK-NOT: ALU8_ADJ
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i16 1
  %b = load i8, ptr %q
  store volatile i8 %b, ptr inttoptr (i16 8960 to ptr)
  %sum = add i8 %a, %initial
  %result = add i8 %sum, %b
  ret i8 %result
}
