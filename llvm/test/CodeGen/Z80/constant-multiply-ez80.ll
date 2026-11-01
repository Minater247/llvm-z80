; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o - | FileCheck %s

target triple = "ez80"

; CHECK-LABEL: mul24:
; CHECK-NOT: call
; CHECK: add hl, hl
; CHECK: ret
define i24 @mul24(i24 %x) {
  %r = mul i24 %x, 17
  ret i24 %r
}
; CHECK-LABEL: sub24:
; CHECK-NOT: call
; CHECK: sbc hl,
; CHECK: ret
define i24 @sub24(i24 %x) {
  %r = mul i24 %x, 7
  ret i24 %r
}
