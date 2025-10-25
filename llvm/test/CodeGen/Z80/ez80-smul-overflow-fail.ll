; NOTE: This documents a known GlobalISel failure on ez80.
; RUN: not --crash llc -mtriple=ez80 < %s 2>&1 | FileCheck %s --check-prefix=ERR
;
; The backend currently cannot select the 64-bit signed-multiply-with-overflow
; intrinsic.  Keep a regression test so we notice once the instruction
; selector grows support.

declare {i64, i1} @llvm.smul.with.overflow.i64(i64, i64)

define i1 @smul_with_overflow_flag(i64 %lhs, i64 %rhs) {
; ERR: LLVM ERROR: cannot select: {{.*}} (in function: smul_with_overflow_flag)
entry:
  %pair = call {i64, i1} @llvm.smul.with.overflow.i64(i64 %lhs, i64 %rhs)
  %flag = extractvalue {i64, i1} %pair, 1
  ret i1 %flag
}
