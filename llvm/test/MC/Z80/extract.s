; RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
; RUN: llvm-readobj --relocations --hex-dump=.text --hex-dump=.data %t.o | FileCheck %s
; RUN: llvm-mc -triple=z80 %s -o %t.s
; RUN: llvm-mc -triple=z80 -filetype=obj %t.s -o %t.roundtrip.o
; RUN: llvm-readobj --relocations --hex-dump=.text --hex-dump=.data %t.roundtrip.o | FileCheck %s

.text
 ld a,external&255
 ld b,(external+257)>>8
 ld c,(external-1)>>16&255
 ld d,external>>24
 ld hl,external&65535
 ld bc,(external+65536)>>16
 ; Parenthesized absolute memory remains distinct from immediate arithmetic.
 ld a,(external+1)
 ld a,(1+2)*3
 ld a,(')'+1)*2
; CHECK: 0x1 R_Z80_BYTE0 external 0x0
; CHECK: 0x3 R_Z80_BYTE1 external 0x101
; CHECK: 0x5 R_Z80_BYTE2 external 0xFFFFFFFF
; CHECK: 0x7 R_Z80_BYTE3 external 0x0
; CHECK: 0x9 R_Z80_WORD0 external 0x0
; CHECK: 0xC R_Z80_WORD1 external 0x10000
; CHECK: 0xF R_Z80_16 external 0x1
; CHECK: Hex dump of section '.text':
; CHECK-NEXT: 0x00000000 3e000600 0e001600 21000001 00003a00
; CHECK-NEXT: 0x00000010 003e093e 54

.data
 db external&255,external>>8,external>>16,external>>24
 dw 65535&external,external>>16
 ; This difference resolves in the assembler, after forward labels are laid out.
 db (end-start)>>8
 dw (end-start)>>16
start:
 .space 0x123
end:
; CHECK: Hex dump of section '.data':
; CHECK-NEXT: 0x00000000 00000000 00000000 01000000 00000000
