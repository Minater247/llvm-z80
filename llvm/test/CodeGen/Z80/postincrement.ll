; RUN: llc -stop-after=z80-register-opt -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mattr=+alternate-regs -stop-after=z80-register-opt -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=BANK
; RUN: %python %S/Inputs/postincrement.py --emit %t.mir
; RUN: llc -mattr=+alternate-regs -start-before=postrapseudos -verify-machineinstrs %t.mir -o %t.s
; RUN: %python %S/Inputs/postincrement.py %t.s
;
; Preserve a destructive unit update with the byte read through its old value.
; Both source orders represent the same operation. Additional old-pointer uses
; retain the original operations and their independent lifetimes.
target triple = "z80"

; BANK-LABEL: name: read_then_step
; BANK-NOT: _POST16
; BANK: RET16
; CHECK-LABEL: name: read_then_step
; CHECK: LD8{{A?}}_POST16
; CHECK-NOT: INC16r
; CHECK: RET16
define i8 @read_then_step(ptr %p, ptr %cursor) {
  %v = load volatile i8, ptr %p
  %next = getelementptr i8, ptr %p, i16 1
  store ptr %next, ptr %cursor
  ret i8 %v
}

; BANK-LABEL: name: step_then_read
; BANK-NOT: _POST16
; BANK: RET16
; CHECK-LABEL: name: step_then_read
; CHECK: LD8{{A?}}_POST16
; CHECK-NOT: INC16r
; CHECK: RET16
define i8 @step_then_read(ptr %p, ptr %cursor) {
  %next = getelementptr i8, ptr %p, i16 1
  %v = load volatile i8, ptr %p
  store ptr %next, ptr %cursor
  ret i8 %v
}

; BANK-LABEL: name: additional_use
; BANK-NOT: _POST16
; BANK: RET16
; CHECK-LABEL: name: additional_use
; CHECK-NOT: _POST16
; CHECK: RET16
define i8 @additional_use(ptr %p, ptr %cursor, ptr %old) {
  %next = getelementptr i8, ptr %p, i16 1
  %v = load volatile i8, ptr %p
  store ptr %p, ptr %old
  store ptr %next, ptr %cursor
  ret i8 %v
}

; BANK-LABEL: name: consumed_before_read
; BANK-NOT: _POST16
; BANK: RET16
; CHECK-LABEL: name: consumed_before_read
; CHECK-NOT: _POST16
; CHECK: RET16
define i8 @consumed_before_read(ptr %p, ptr %cursor) {
  %next = getelementptr i8, ptr %p, i16 1
  store volatile ptr %next, ptr %cursor
  %v = load volatile i8, ptr %p
  ret i8 %v
}

; BANK-LABEL: name: scan_until_zero
; BANK: LD8A_POST16
define ptr @scan_until_zero(ptr %start) {
entry:
  br label %loop
loop:
  %p = phi ptr [ %start, %entry ], [ %next, %loop ]
  %v = load volatile i8, ptr %p
  %next = getelementptr i8, ptr %p, i16 1
  %more = icmp ne i8 %v, 0
  br i1 %more, label %loop, label %exit
exit:
  ret ptr %next
}
