; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o - | FileCheck %s

target triple = "z80"

; Preserve the bound on expansion and leave other products to legalization.
; CHECK-LABEL: mul30:
; CHECK: __mulhi3
define i16 @mul30(i16 %x) minsize {
  %r = mul i16 %x, 30
  ret i16 %r
}
; CHECK-LABEL: mul11:
; CHECK: __mulhi3
define i16 @mul11(i16 %x) minsize {
  %r = mul i16 %x, 11
  ret i16 %r
}
