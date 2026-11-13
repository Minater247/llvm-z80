; RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
; RUN: llvm-readobj --hex-dump=.text --relocations %t.o | FileCheck %s
; RUN: llvm-mc -triple=z80 %s -o %t.s
; RUN: llvm-mc -triple=z80 -filetype=obj %t.s -o %t.roundtrip.o
; RUN: llvm-readobj --hex-dump=.text --relocations %t.roundtrip.o | FileCheck %s

; Numeric operands are absolute destinations; $ names the instruction address.
.space 16
 jr 0
 jr $-2
 djnz after
 nop
after:
 jr nz,after
 jr external
; CHECK: 0x11 R_Z80_8_PCREL - 0xFFFFFFFF
; CHECK: R_Z80_8_PCREL external 0xFFFFFFFF
; CHECK: Hex dump of section '.text':
; CHECK: 0x00000010 180018fc 10010020 fe1800
