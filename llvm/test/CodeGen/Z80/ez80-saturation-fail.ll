; NOTE: This documents that ez80 currently crashes when lowering the
; saturated fixed-point multiply intrinsic for 64-bit operands.
; RUN: not --crash llc -mtriple=ez80 < %s 2>&1 | FileCheck %s --check-prefix=SAT

declare i64 @llvm.smul.fix.sat.i64(i64, i64, i32 immarg)

define i64 @smul_fix_sat(i64 %lhs, i64 %rhs) {
; SAT: LLVM ERROR: cannot select: {{.*}} (in function: smul_fix_sat)
entry:
  %res = call i64 @llvm.smul.fix.sat.i64(i64 %lhs, i64 %rhs, i32 31)
  ret i64 %res
}
