; RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
; RUN: llvm-readobj --hex-dump=.data --relocations %t.o | FileCheck %s
; RUN: llvm-mc -triple=z80 %s -o %t.s
; RUN: llvm-mc -triple=z80 -filetype=obj %t.s -o %t.roundtrip.o
; RUN: llvm-objcopy --dump-section .data=%t.bin %t.o
; RUN: llvm-objcopy --dump-section .data=%t.roundtrip.bin %t.roundtrip.o
; RUN: cmp %t.bin %t.roundtrip.bin

.data
entry:
 db 'A',"B\"C",0
 .defb 255
 dw 0x1234
 .defw -1
 d24 0x123456
 d32 0x12345678
 ; The count depends on labels defined after this fill fragment.
 ds end-start,0xaa
start:
 db 1,2,3
end:
 .defs 32-($-entry),0xbb
 d24 external+3
 d32 external+4
; CHECK: R_Z80_24 external 0x3
; CHECK: R_Z80_32 external 0x4
; CHECK: Hex dump of section '.data':
; CHECK-NEXT: 0x00000000 41422243 00ff3412 ffff5634 12785634
; CHECK-NEXT: 0x00000010 12aaaaaa 010203bb bbbbbbbb bbbbbbbb
; CHECK-NEXT: 0x00000020 00000000 000000
