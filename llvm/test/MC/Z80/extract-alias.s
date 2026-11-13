; RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
; RUN: llvm-readobj --relocations --symbols %t.o | FileCheck %s
; RUN: llvm-mc -triple=z80 %s -o %t.s
; RUN: llvm-mc -triple=z80 -filetype=obj %t.s -o %t.roundtrip.o
; RUN: llvm-readobj --relocations --symbols %t.roundtrip.o | FileCheck %s
; RUN: llvm-mc -triple=ez80-unknown-none-code16 -filetype=obj %s -o %t.ez80.o
; RUN: llvm-readobj --relocations --symbols %t.ez80.o | FileCheck %s

; Alias placement and chaining do not change instruction/data relocations.
.text
.set high, (external+257)>>8
 ld a,high
 ld b,forward
 ld hl,word
 ld de,middle
.set forward, chained
.set chained, (external-1)>>16&255
.set word, (external+65536)>>16
.set middle, (external+384)>>8&65535

; CHECK: Section {{.*}} .rela.text {
; CHECK-NEXT: 0x1 R_Z80_BYTE1 external 0x101
; CHECK-NEXT: 0x3 R_Z80_BYTE2 external 0xFFFFFFFF
; CHECK-NEXT: 0x5 R_Z80_WORD1 external 0x10000
; CHECK-NEXT: 0x8 R_Z80_BYTE1 external 0x180
; CHECK-NEXT: 0x9 R_Z80_BYTE2 external 0x180
; CHECK-NEXT: }

.data
 db high,forward
 dw word,middle
 d24 triple,low24
 d32 low32
.set triple, (external+256)>>8&16777215
.set low24, (external-1)&16777215
.set low32, (external+1)&4294967295

; CHECK: Section {{.*}} .rela.data {
; CHECK-DAG: 0x0 R_Z80_BYTE1 external 0x101
; CHECK-DAG: 0x1 R_Z80_BYTE2 external 0xFFFFFFFF
; CHECK-DAG: 0x2 R_Z80_WORD1 external 0x10000
; CHECK-DAG: 0x4 R_Z80_BYTE1 external 0x180
; CHECK-DAG: 0x5 R_Z80_BYTE2 external 0x180
; CHECK-DAG: 0x6 R_Z80_BYTE1 external 0x100
; CHECK-DAG: 0x7 R_Z80_BYTE2 external 0x100
; CHECK-DAG: 0x8 R_Z80_BYTE3 external 0x100
; CHECK-DAG: 0x9 R_Z80_BYTE0 external 0xFFFFFFFF
; CHECK-DAG: 0xA R_Z80_BYTE1 external 0xFFFFFFFF
; CHECK-DAG: 0xB R_Z80_BYTE2 external 0xFFFFFFFF
; CHECK-DAG: 0xC R_Z80_BYTE0 external 0x1
; CHECK-DAG: 0xD R_Z80_BYTE1 external 0x1
; CHECK-DAG: 0xE R_Z80_BYTE2 external 0x1
; CHECK-DAG: 0xF R_Z80_BYTE3 external 0x1
; CHECK: }

; Absolute assignments retain their ordinary symbol-table entries.
.set resolved, (end-start)>>8
.section .resolved,"a",@progbits
 db resolved
start:
.space 0x123
end:
; CHECK: Name: resolved
; CHECK-NEXT: Value: 0x1
