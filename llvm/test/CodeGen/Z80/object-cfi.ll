; RUN: llc -mtriple=z80 -filetype=obj %s -o %t.z80.o
; RUN: llvm-dwarfdump --eh-frame %t.z80.o | FileCheck %s --check-prefixes=COMMON,Z80
; RUN: llc -mtriple=ez80 -filetype=obj %s -o %t.ez80.o
; RUN: llvm-dwarfdump --eh-frame %t.ez80.o | FileCheck %s --check-prefixes=COMMON,ADL
; RUN: llc -mtriple=ez80 %s -o %t.s
; RUN: llvm-mc -triple=ez80 -filetype=obj %t.s -o %t.asm.o
; RUN: llvm-dwarfdump --eh-frame %t.asm.o | FileCheck %s --check-prefixes=COMMON,ADL
; RUN: llc -mtriple=ez80 -force-dwarf-frame-section -filetype=obj %s -o %t.debug.o
; RUN: llvm-dwarfdump --debug-frame %t.debug.o | FileCheck %s --check-prefixes=COMMON,ADL

; The return address is PC, and saved IX has a signed CFA-relative offset.
; In ADL mode an unsigned offset is not even divisible by the slot size.
; COMMON: Return address column: 8
; Z80: DW_CFA_offset: PC -2
; ADL: DW_CFA_offset: PC -3
; COMMON: FDE cie=
; Z80: DW_CFA_offset: IX -4
; ADL: DW_CFA_offset: IX -6

declare void @use(ptr)

define void @frame() uwtable "frame-pointer"="all" {
  %slot = alloca i8, align 1
  call void @use(ptr %slot)
  ret void
}
