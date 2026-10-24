; The memcpy cursor has advanced by seven. Reusing it for dst+5 needs a
; negative, typed G_CONSTANT rather than an immediate operand on G_CONSTANT.
; RUN: llc -verify-machineinstrs -stop-after=z80-early-incremental-loading %s -o - | FileCheck %s
; RUN: llc -verify-machineinstrs %s -o /dev/null
; CHECK: G_CONSTANT i16 -2
; CHECK-NEXT: {{.*}}G_PTR_ADD

target triple = "z80"
declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1)
define i16 @copy_tail(ptr %dst, ptr %src) {
  call void @llvm.memcpy.p0.p0.i16(ptr %dst, ptr %src, i16 7, i1 false)
  %tail = getelementptr i8, ptr %dst, i16 5
  %v = load i16, ptr %tail, align 1
  ret i16 %v
}
