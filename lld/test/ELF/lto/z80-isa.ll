; REQUIRES: z80
; RUN: split-file %s %t
; RUN: llvm-as %t/base.ll -o %t/base.o
; RUN: llvm-as %t/extended.ll -o %t/extended.o
; RUN: ld.lld -e base -u extended %t/base.o %t/extended.o -o %t/full
; RUN: llvm-readobj -h %t/full | FileCheck %s
; RUN: opt -module-summary %t/base.ll -o %t/base.o
; RUN: opt -module-summary %t/extended.ll -o %t/extended.o
; RUN: ld.lld -e base -u extended %t/base.o %t/extended.o -o %t/thin
; RUN: llvm-readobj -h %t/thin | FileCheck %s
; CHECK: Flags [ (0x4)
;
;--- base.ll
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"
define i8 @base() "target-cpu"="z80" {
  ret i8 1
}
;--- extended.ll
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "ez80-unknown-none-code16"
define i8 @extended() "target-cpu"="ez80" {
  ret i8 2
}
