; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=SELECT
; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o - | FileCheck %s
target triple = "z80"

; A zero test has a distinct comparison descriptor, allowing flag reuse from
; the byte arithmetic even when its result passes through accumulator copies.
; SELECT-LABEL: name: zero_compare
; SELECT: OR8aa implicit-def $f, implicit $a
; CHECK-LABEL: zero_compare:
; CHECK-NOT: {{^[ \t]+or[ \t]+a}}
; CHECK: ret
define i8 @zero_compare(ptr %p, ptr %out) {
  %value = load volatile i8, ptr %p
  %next = add i8 %value, -1
  store volatile i8 %next, ptr %out
  %zero = icmp eq i8 %next, 0
  br i1 %zero, label %yes, label %no
yes:
  ret i8 1
no:
  ret i8 0
}
