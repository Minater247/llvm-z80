; REQUIRES: z80
; RUN: split-file %s %t
; RUN: llvm-as %t/entry.ll -o %t/entry.o
; RUN: llvm-as %t/helper.ll -o %t/helper.o
; RUN: llvm-as %t/unused.ll -o %t/unused.o
; RUN: rm -f %t/lib.a
; RUN: llvm-ar rc %t/lib.a %t/helper.o %t/unused.o
; RUN: ld.lld -m elf32z80 -e entry -T %t/link.ld --gc-sections %t/entry.o %t/lib.a -o %t/full
; RUN: llvm-readobj -h --hex-dump=.text --symbols %t/full | FileCheck %s
; RUN: opt -module-summary %t/entry.ll -o %t/entry.o
; RUN: opt -module-summary %t/helper.ll -o %t/helper.o
; RUN: opt -module-summary %t/unused.ll -o %t/unused.o
; RUN: llvm-ar rc %t/lib.a %t/helper.o %t/unused.o
; RUN: ld.lld -e entry -T %t/link.ld --gc-sections %t/entry.o %t/lib.a -o %t/thin
; RUN: llvm-readobj -h --hex-dump=.text --symbols %t/thin | FileCheck %s
;
; The linker must extract the required bitcode archive member, fold its call,
; retain state accessed outside C via EXTERN and KEEP, and discard the unused member.
; CHECK: Machine: EM_Z80
; CHECK: Entry: 0x200
; CHECK: Flags [ (0x1)
; CHECK-DAG: Name: entry
; CHECK-DAG: Value: 0x200
; CHECK-DAG: Name: shared
; CHECK-DAG: Value: 0x1800
; CHECK-NOT: Name: unused
; CHECK: Hex dump of section '.text':
; CHECK-NEXT: 0x00000200 3e2a3210 18320018 c9
;
;--- link.ld
OUTPUT_FORMAT(elf32-z80)
EXTERN(shared)
MEMORY { rom (rx) : ORIGIN = 0x200, LENGTH = 0x1000
         ram (rw) : ORIGIN = 0x1800, LENGTH = 0x100 }
SECTIONS {
 .text : { *(.text*) } > rom
 .shared : { KEEP(*(.shared)) } > ram
 .data : { *(.data*) } > ram
 .bss (NOLOAD) : { *(.bss*) } > ram
}
port = 0x1810;

;--- entry.ll
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"
@shared = global i8 0, section ".shared", align 1
@port = external global i8
declare i8 @helper(i8)
define i8 @entry() {
  %r = call i8 @helper(i8 41)
  store volatile i8 %r, ptr @port
  store i8 %r, ptr @shared
  ret i8 %r
}

;--- helper.ll
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"
define i8 @helper(i8 %x) {
  %r = add i8 %x, 1
  ret i8 %r
}

;--- unused.ll
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"
define i8 @unused() {
  ret i8 99
}
