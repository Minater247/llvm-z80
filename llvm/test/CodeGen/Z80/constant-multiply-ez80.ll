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

; CHECK-LABEL: even24:
; CHECK-NOT: call
; CHECK: add hl, hl
; CHECK: ret
define i24 @even24(i24 %x) {
  %r = mul i24 %x, 24
  ret i24 %r
}
; CHECK-LABEL: even14:
; CHECK-NOT: call
; CHECK: sbc hl,
; CHECK: add hl, hl
; CHECK: ret
define i24 @even14(i24 %x) {
  %r = mul i24 %x, 14
  ret i24 %r
}
