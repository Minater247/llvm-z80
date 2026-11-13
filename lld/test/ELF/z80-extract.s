# REQUIRES: z80
# RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
# RUN: ld.lld -m elf32z80 -e 0 -Ttext=0 --defsym=external=0x12345678 %t.o -o %t
# RUN: llvm-readobj --hex-dump=.text %t | FileCheck %s

.text
 ld a,external&255
 ld b,(external+257)>>8
 ld c,(external-1)>>16&255
 ld d,external>>24
 ld hl,external&65535
 ld bc,(external+65536)>>16
 db external&255,external>>8,external>>16,external>>24
 dw 65535&external,external>>16
# CHECK: Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 3e780657 0e341612 21785601 35127856
# CHECK-NEXT: 0x00000010 34127856 3412
