; REQUIRES: z80-registered-target
; RUN: split-file %s %t
; RUN: llvm-as %t/a.ll -o %t/a.bc
; RUN: llvm-as %t/b.ll -o %t/b.bc
; RUN: llvm-lto2 run %t/a.bc %t/b.bc -o %t/full -r %t/a.bc,entry,plx -r %t/a.bc,helper,l -r %t/b.bc,helper,pl
; RUN: llvm-readobj --file-headers --hex-dump=.text %t/full.0 | FileCheck %s
; RUN: opt -module-summary %t/a.ll -o %t/a.thin.bc
; RUN: opt -module-summary %t/b.ll -o %t/b.thin.bc
; RUN: llvm-lto2 run %t/a.thin.bc %t/b.thin.bc -o %t/thin -r %t/a.thin.bc,entry,plx -r %t/a.thin.bc,helper,l -r %t/b.thin.bc,helper,pl
; RUN: llvm-readobj --file-headers --hex-dump=.text %t/thin.1 | FileCheck %s
;
; Both LTO paths inline and fold the cross-module call, then emit a native
; Z80 object containing LD A,42; RET. No assembly text conversion is involved.
; CHECK: Format: elf32-z80
; CHECK: Machine: EM_Z80
; CHECK: Flags [ (0x1)
; CHECK: Hex dump of section '.text':
; CHECK-NEXT: 0x00000000 3e2ac9
;
;--- a.ll
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"
declare i8 @helper(i8)
define i8 @entry() {
  %r = call i8 @helper(i8 41)
  ret i8 %r
}
;--- b.ll
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"
define i8 @helper(i8 %x) {
  %r = add i8 %x, 1
  ret i8 %r
}
