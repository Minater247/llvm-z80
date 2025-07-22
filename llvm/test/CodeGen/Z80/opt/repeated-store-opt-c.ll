; Test make sure that the optimization pass works correctly
; RUN: llc -mtriple=z80 -O2 < %s | FileCheck %s

; This is the C code `for (int i = 0; i < 256; i++) { *(volatile char*)0x8000 = i; }`

define void @c_style_loop_example() {
; CHECK-LABEL: c_style_loop_example:
; CHECK:       ld {{[abcdehl]+}}, {{[0-9-]+}}

entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %i.08 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %conv = trunc i32 %i.08 to i8
  store volatile i8 %conv, i8* inttoptr (i64 32768 to i8*), align 1
  %inc = add nuw nsw i32 %i.08, 1
  %exitcond.not = icmp eq i32 %inc, 256
  br i1 %exitcond.not, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret void
}

; This is what should be optimized - multiple stores within one block
define void @optimizable_sequential_stores() {
; CHECK-LABEL: optimizable_sequential_stores:
; CHECK:       ld hl, -32768
; CHECK:       ld (hl), a
; CHECK:       ld (hl), a
; CHECK:       ld (hl), a

entry:
  store volatile i8 1, i8* inttoptr (i64 32768 to i8*), align 1
  store volatile i8 3, i8* inttoptr (i64 32768 to i8*), align 1
  store volatile i8 7, i8* inttoptr (i64 32768 to i8*), align 1
  ret void
}