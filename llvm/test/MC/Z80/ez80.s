; RUN: llvm-mc -triple=ez80 -show-encoding %s | FileCheck %s
; RUN: llvm-mc -triple=ez80 %s -o %t.s
; RUN: llvm-mc -triple=ez80 -filetype=obj %s -o %t.o
; RUN: llvm-mc -triple=ez80 -filetype=obj %t.s -o %t.roundtrip.o
; RUN: llvm-objcopy --dump-section .text=%t.bin %t.o
; RUN: llvm-objcopy --dump-section .text=%t.roundtrip.bin %t.roundtrip.o
; RUN: cmp %t.bin %t.roundtrip.bin

 ld bc,0x123456
 ld.sis bc,0x1234
 ld.lis bc,0x1234
 ld.sil bc,0x123456
 ld.lil bc,0x123456
 ld.sis a,(0x1234)
 ld.lil a,(0x123456)
 lea hl,ix-5
 lea de,iy
 pea ix
 ld hl,(iy+7)
 ex (sp),hl
 adc hl,de
 in a,(c)
 out (bc),b
; CHECK: ld bc, 1193046 {{.*}} [0x01,0x56,0x34,0x12]
; CHECK: ld.sis bc, 4660 {{.*}} [0x40,0x01,0x34,0x12]
; CHECK: ld.lis bc, 4660 {{.*}} [0x49,0x01,0x34,0x12]
; CHECK: ld.sil bc, 1193046 {{.*}} [0x52,0x01,0x56,0x34,0x12]
; CHECK: ld.lil bc, 1193046 {{.*}} [0x5b,0x01,0x56,0x34,0x12]
; CHECK: ld.sis a, (4660) {{.*}} [0x40,0x3a,0x34,0x12]
; CHECK: ld.lil a, (1193046) {{.*}} [0x5b,0x3a,0x56,0x34,0x12]
; CHECK: lea hl, ix - 5 {{.*}} [0xed,0x22,0xfb]
; CHECK: lea de, iy {{.*}} [0xed,0x13,0x00]
; CHECK: pea ix {{.*}} [0xed,0x65,0x00]
; CHECK: ld hl, (iy + 7) {{.*}} [0xfd,0x27,0x07]
; CHECK: ex (sp), hl {{.*}} [0xe3]
; CHECK: adc hl, de {{.*}} [0xed,0x5a]
; CHECK: in a, (bc) {{.*}} [0xed,0x78]
; CHECK: out (bc), b {{.*}} [0xed,0x41]
.code16
 ld bc,0x5678
; CHECK: .code16
; CHECK: ld bc, 22136 {{.*}} [0x01,0x78,0x56]
.code24
 ld bc,0x123456
; CHECK: .code24
; CHECK: ld bc, 1193046 {{.*}} [0x01,0x56,0x34,0x12]
