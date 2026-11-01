; RUN: opt -passes=indvars -S %s | FileCheck %s
; RUN: opt -passes=indvars -S %s | llc -verify-machineinstrs -o /dev/null
; RUN: opt -passes=indvars -S %s | llc -regalloc=greedy -verify-machineinstrs -o /dev/null
;
; A byte counter that indexes memory need not occupy a register pair throughout
; the loop. Widening its addition is more expensive on the Z80.
; CHECK-LABEL: define void @copy17(
; CHECK: %i = phi i8
; CHECK: %index = zext i8 %i to i16
; CHECK: %next = add nuw nsw i8 %i, 1
; CHECK: %done = icmp eq i8 %next, 17

target triple = "z80"
define void @copy17(ptr %dst, ptr %src) {
entry:
  br label %loop
loop:
  %i = phi i8 [ 0, %entry ], [ %next, %loop ]
  %index = zext i8 %i to i16
  %p = getelementptr i8, ptr %src, i16 %index
  %q = getelementptr i8, ptr %dst, i16 %index
  %v = load i8, ptr %p
  store i8 %v, ptr %q
  %next = add nuw i8 %i, 1
  %done = icmp eq i8 %next, 17
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
