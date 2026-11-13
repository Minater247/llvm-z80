; RUN: llvm-mc -triple=z80 -show-encoding %s | FileCheck %s
; RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
; RUN: llvm-readobj --relocations %t.o | FileCheck %s --check-prefix=RELOC

.text
.global entry
entry:
 ld bc,0x1234
 ld a,(bc)
 ld (de),a
 ld h,(ix-2)
 ld (iy+3),l
 ld ix,0x1234
 inc hl
 add hl,bc
 adc hl,de
 ld sp,hl
 bit 3,a
 bit 4,(hl)
 rlc (ix+5),b
 ex af,af'
 ex (sp),iy
 jp (hl)
 call c,external
 ret z
 ret
; CHECK: ld bc, 4660 {{.*}} [0x01,0x34,0x12]
; CHECK: ld a, (bc) {{.*}} [0x0a]
; CHECK: ld (de), a {{.*}} [0x12]
; CHECK: ld h, (ix - 2) {{.*}} [0xdd,0x66,0xfe]
; CHECK: ld (iy + 3), l {{.*}} [0xfd,0x75,0x03]
; CHECK: ld ix, 4660 {{.*}} [0xdd,0x21,0x34,0x12]
; CHECK: inc hl {{.*}} [0x23]
; CHECK: add hl, bc {{.*}} [0x09]
; CHECK: adc hl, de {{.*}} [0xed,0x5a]
; CHECK: ld sp, hl {{.*}} [0xf9]
; CHECK: bit 3, a {{.*}} [0xcb,0x5f]
; CHECK: bit 4, (hl) {{.*}} [0xcb,0x66]
; CHECK: rlc (ix + 5), b {{.*}} [0xdd,0xcb,0x05,0x00]
; CHECK: ex af, af' {{.*}} [0x08]
; CHECK: ex (sp), iy {{.*}} [0xfd,0xe3]
; CHECK: jp (hl) {{.*}} [0xe9]
; CHECK: call c, external {{.*}} [0xdc,A,A]
; CHECK: ret z {{.*}} [0xc8]
; CHECK: ret {{.*}} [0xc9]
; RELOC: R_Z80_16 external 0x0
; RELOC: R_Z80_16 external 0x3

.data
 db "hi",0
 dw external+3
 ds 2,0xaa
