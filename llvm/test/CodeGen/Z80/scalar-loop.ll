; RUN: opt -passes=loop-vectorize -force-vector-width=4 -S %s -o %t.ll
; RUN: FileCheck %s --implicit-check-not="<4 x" < %t.ll
; RUN: llc -verify-machineinstrs %t.ll -o /dev/null
; RUN: opt -mtriple=ez80 -passes=loop-vectorize -force-vector-width=4 -S %s -o %t.ez80.ll
; RUN: FileCheck %s --implicit-check-not="<4 x" < %t.ez80.ll
; RUN: llc -verify-machineinstrs %t.ez80.ll -o /dev/null
;
; Generic vector-width settings must leave scalar-only targets with scalar
; memory operations and arithmetic that their instruction selectors support.
; CHECK-LABEL: define void @add_bytes
; CHECK: load i8,
; CHECK: add i8
; CHECK: store i8
; CHECK: ret void

target triple = "z80"
define void @add_bytes(ptr noalias %dst, ptr noalias readonly %src, i16 %count) {
entry:
  %empty = icmp eq i16 %count, 0
  br i1 %empty, label %exit, label %loop
loop:
  %i = phi i16 [ 0, %entry ], [ %next, %loop ]
  %s = getelementptr i8, ptr %src, i16 %i
  %v = load i8, ptr %s
  %a = add i8 %v, 7
  %d = getelementptr i8, ptr %dst, i16 %i
  store i8 %a, ptr %d
  %next = add i16 %i, 1
  %done = icmp eq i16 %next, %count
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
