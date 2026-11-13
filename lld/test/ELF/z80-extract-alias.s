# REQUIRES: z80
# RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
# RUN: ld.lld -m elf32z80 -e 0 -Ttext=0 --defsym=external=0x12345678 %t.o -o %t
# RUN: llvm-readobj --hex-dump=.text --hex-dump=.data --hex-dump=.resolved %t | FileCheck %s
# CHECK: Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 3e570634 21351211 5734
# CHECK: Hex dump of section '.resolved':
# CHECK-NEXT: {{0x[0-9a-f]+}} 01000000
# CHECK: Hex dump of section '.data':
# CHECK-NEXT: {{0x[0-9a-f]+}} 57343512 57345734 12775634 79563412

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
.data
 db high,forward
 dw word,middle
 d24 triple,low24
 d32 low32
.set triple, (external+256)>>8&16777215
.set low24, (external-1)&16777215
.set low32, (external+1)&4294967295
.set resolved, (end-start)>>8
.section .resolved,"a",@progbits
 db resolved
start:
.space 0x123
end:
