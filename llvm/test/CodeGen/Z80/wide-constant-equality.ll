; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/wide-constant-equality.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: FileCheck %s < %t.bank.s
; RUN: %python %S/Inputs/wide-constant-equality.py %t.bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; Equality against a word constant needs only differing byte bits. Keep all
; four bytes significant, and preserve a compared value used after the branch.
target triple = "z80"

; CHECK-LABEL: mixed:
; CHECK-NOT: call
; CHECK: ret
define i8 @mixed(ptr %p) optsize {
  %v = load i32, ptr %p, align 1
  %test = icmp eq i32 %v, 305419896
  br i1 %test, label %yes, label %no
yes:
  ret i8 31
no:
  ret i8 79
}

; CHECK-LABEL: zero:
; CHECK-NOT: call
; CHECK: ret
define i8 @zero(ptr %p) optsize {
  %v = load i32, ptr %p, align 1
  %test = icmp ne i32 %v, 0
  br i1 %test, label %yes, label %no
yes:
  ret i8 31
no:
  ret i8 79
}

; CHECK-LABEL: all_ones:
; CHECK-NOT: call
; CHECK: ret
define i8 @all_ones(ptr %p) optsize {
  %v = load i32, ptr %p, align 1
  %test = icmp eq i32 %v, -1
  br i1 %test, label %yes, label %no
yes:
  ret i8 31
no:
  ret i8 79
}

; CHECK-LABEL: retained:
; CHECK-NOT: call
; CHECK: ret
define i16 @retained(ptr %p) optsize {
  %v = load i32, ptr %p, align 1
  %test = icmp eq i32 %v, 16777215
  br i1 %test, label %yes, label %no
yes:
  ret i16 37
no:
  %high = lshr i32 %v, 16
  %lo = trunc i32 %v to i16
  %hi = trunc i32 %high to i16
  %result = xor i16 %lo, %hi
  ret i16 %result
}
