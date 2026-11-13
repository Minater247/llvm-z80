; REQUIRES: z80
; RUN: split-file %s %t
; RUN: llvm-as %t/z80.ll -o %t/z80.o
; RUN: llvm-as %t/ez80.ll -o %t/ez80.o
; RUN: not ld.lld -r %t/z80.o %t/ez80.o -o /dev/null 2>&1 | FileCheck %s
; RUN: not ld.lld -r %t/ez80.o %t/z80.o -o /dev/null 2>&1 | FileCheck %s
; RUN: llc -filetype=obj %t/z80.ll -o %t/native.o
; RUN: not ld.lld -r %t/ez80.o %t/native.o -o /dev/null 2>&1 | FileCheck %s
; RUN: opt -module-summary %t/z80.ll -o %t/z80.o
; RUN: opt -module-summary %t/ez80.ll -o %t/ez80.o
; RUN: not ld.lld -r %t/z80.o %t/ez80.o -o /dev/null 2>&1 | FileCheck %s
; CHECK: cannot link Z80 16-bit and eZ80 ADL objects
;
;--- z80.ll
target datalayout = "e-m:e-p:16:8-i16:8-n8:16-S8"
target triple = "z80"
@z80 = global ptr null
;--- ez80.ll
target datalayout = "e-m:e-p:24:8-i16:8-i24:8-n8:16:24-S8"
target triple = "ez80"
@ez80 = global ptr null
