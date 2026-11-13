; RUN: llc -mtriple=z80 -filetype=obj %s -o %t.z80.o
; RUN: llvm-readobj -h %t.z80.o | FileCheck %s --check-prefix=CODE16
; RUN: llc -mtriple=ez80 -filetype=obj %s -o %t.adl.o
; RUN: llvm-readobj -h %t.adl.o | FileCheck %s --check-prefix=ADL
; RUN: llc -mtriple=z80 -mcpu=z180 -filetype=obj %s -o %t.z180.o
; RUN: llvm-readobj -h %t.z180.o | FileCheck %s --check-prefix=CODE16
;
; A per-function CPU can raise the object's ISA requirement while preserving
; the module's ABI. This is also needed after LTO combines different CPUs.
; CODE16: Flags [ (0x4)
; ADL: Flags [ (0x84)

define i8 @base() {
  ret i8 1
}

define i8 @extended() "target-cpu"="ez80" {
  ret i8 2
}
