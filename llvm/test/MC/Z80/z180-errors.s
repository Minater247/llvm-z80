; RUN: not llvm-mc -triple=z80 -mcpu=z180 %s -o /dev/null 2>&1 | FileCheck %s
; RUN: not llvm-mc -triple=ez80 %s -o /dev/null 2>&1 | FileCheck %s

tst (ix)
tst a,(iy)
tst a,(ix+1)
tst a,(bc)
tst ixh
mlt ix
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
; CHECK: error: invalid operand for instruction
