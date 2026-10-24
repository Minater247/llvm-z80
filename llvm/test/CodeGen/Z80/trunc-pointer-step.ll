; Narrow a page-wrapped step, but retain a shared full-width pointer result.
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/trunc-pointer-step.py %t.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o - | FileCheck %s

target triple = "z80"

; CHECK-LABEL: pointer_inc:
; CHECK: inc {{[el]}}
; CHECK: ret
define i8 @pointer_inc(ptr %x) {
  %p = getelementptr i8, ptr %x, i16 1
  %low = ptrtoint ptr %p to i8
  ret i8 %low
}

; CHECK-LABEL: pointer_dec:
; CHECK: dec {{[el]}}
; CHECK: ret
define i8 @pointer_dec(ptr %x) {
  %p = getelementptr i8, ptr %x, i16 -1
  %low = ptrtoint ptr %p to i8
  ret i8 %low
}

; CHECK-LABEL: shared:
; CHECK: inc de
; CHECK: ret
define i8 @shared(ptr %x, ptr %out) {
  %p = getelementptr i8, ptr %x, i16 1
  store ptr %p, ptr %out
  %low = ptrtoint ptr %p to i8
  ret i8 %low
}

; CHECK-LABEL: pointer_inc2:
; CHECK: inc {{[el]}}
; CHECK-NEXT: inc {{[el]}}
; CHECK: ret
define i8 @pointer_inc2(ptr %x) {
  %p = getelementptr i8, ptr %x, i16 2
  %low = ptrtoint ptr %p to i8
  ret i8 %low
}

; CHECK-LABEL: pointer_dec2:
; CHECK: dec {{[el]}}
; CHECK-NEXT: dec {{[el]}}
; CHECK: ret
define i8 @pointer_dec2(ptr %x) {
  %p = getelementptr i8, ptr %x, i16 -2
  %low = ptrtoint ptr %p to i8
  ret i8 %low
}
