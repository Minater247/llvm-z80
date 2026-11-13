; RUN: not llvm-mc -triple=z80 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s
add hl,ix
add ix,iy
ld h,ixl
ld (bc),7
inc (bc)
bit 8,a
bit foo,a
ret 7
jr pe,.
im 3
rst 9
ld a,256
ld a,(ix+128)
.code24
ld.lil bc,1
mlt bc
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: Z80 bit number out of range
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid Z80 branch condition
; CHECK: error: invalid Z80 interrupt mode
; CHECK: error: invalid Z80 restart address
; CHECK: error: Z80 immediate out of range
; CHECK: error: Z80 displacement out of range
; CHECK: error: .code24 requires eZ80
; CHECK: error: instruction suffix requires eZ80
; CHECK: error: instruction requires an unavailable Z80 CPU feature
