; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/rmw-result.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/rmw-result.py %t.greedy.s
; RUN: llc -stop-after=z80-postselect-combiner -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/rmw-result.py %t.bank.s
; A memory increment with a live result needs a load-only reload MMO.
target triple = "z80"
define i8 @increment(ptr %p) {
  %v = load i8, ptr %p
  %n = add i8 %v, 1
  store i8 %n, ptr %p
  ret i8 %n
}
define i8 @decrement(ptr %p) {
  %v = load i8, ptr %p
  %n = add i8 %v, -1
  store i8 %n, ptr %p
  ret i8 %n
}

; Reuse the update's zero flag before load folding hides its virtual result.
; CHECK-LABEL: name: increment_zero
; CHECK: INC8{{[rpo]}}
; CHECK-NOT: OR8aa
; CHECK: JQCC
define i8 @increment_zero(ptr %base) {
  %p = getelementptr i8, ptr %base, i16 0
  %v = load i8, ptr %p
  %n = add i8 %v, 1
  store i8 %n, ptr %p
  %zero = icmp eq i8 %n, 0
  br i1 %zero, label %yes, label %no
yes:
  ret i8 17
no:
  ret i8 93
}

; Reuse the update's zero flag before load folding hides its virtual result.
; CHECK-LABEL: name: decrement_zero
; CHECK: DEC8{{[po]}}
; CHECK-NOT: OR8aa
; CHECK: JQCC
define i8 @decrement_zero(ptr %base) {
  %p = getelementptr i8, ptr %base, i16 0
  %v = load i8, ptr %p
  %n = add i8 %v, -1
  store i8 %n, ptr %p
  %zero = icmp ne i8 %n, 0
  br i1 %zero, label %yes, label %no
yes:
  ret i8 17
no:
  ret i8 93
}

; Reuse the update's zero flag before load folding hides its virtual result.
; CHECK-LABEL: name: offset_zero
; CHECK: DEC8{{[po]}}
; CHECK-NOT: OR8aa
; CHECK: JQCC
define i8 @offset_zero(ptr %base) {
  %p = getelementptr i8, ptr %base, i16 5
  %v = load i8, ptr %p
  %n = add i8 %v, -1
  store i8 %n, ptr %p
  %zero = icmp eq i8 %n, 0
  br i1 %zero, label %yes, label %no
yes:
  ret i8 17
no:
  ret i8 93
}

; An explicit hardware reread observes a new value and needs its own test.
; CHECK-LABEL: name: ordered_reread
; CHECK: DEC8{{[po]}}
; CHECK: OR8aa
; CHECK: JQCC
define i8 @ordered_reread(ptr %p) {
  %v = load volatile i8, ptr %p
  %n = add i8 %v, -1
  store volatile i8 %n, ptr %p
  %again = load volatile i8, ptr %p
  %zero = icmp eq i8 %again, 0
  br i1 %zero, label %yes, label %no
yes:
  ret i8 17
no:
  ret i8 93
}
