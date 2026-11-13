; RUN: llvm-mc -triple=z80 -mcpu=z180 -show-encoding %s | FileCheck %s
; RUN: llvm-mc -triple=ez80 -show-encoding %s | FileCheck %s
; RUN: llvm-mc -triple=z80 -mcpu=z180 -filetype=obj %s -o %t.o
; RUN: llvm-readobj --file-headers %t.o | FileCheck %s --check-prefix=FLAGS
; RUN: llvm-mc -triple=z80 -mcpu=z180 %s -o %t.s
; RUN: llvm-mc -triple=z80 -mcpu=z180 -filetype=obj %t.s -o %t.roundtrip.o
; RUN: llvm-objcopy --dump-section .text=%t.bin %t.o
; RUN: llvm-objcopy --dump-section .text=%t.roundtrip.bin %t.roundtrip.o
; RUN: cmp %t.bin %t.roundtrip.bin
; RUN: not llvm-mc -triple=z80 -mcpu=z80 %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=Z80

; FLAGS: Flags [ (0x2)
; Z80-COUNT-21: error: instruction requires an unavailable Z80 CPU feature

mlt bc
mlt de
mlt hl
mlt sp
; CHECK: mlt bc {{.*}} [0xed,0x4c]
; CHECK: mlt de {{.*}} [0xed,0x5c]
; CHECK: mlt hl {{.*}} [0xed,0x6c]
; CHECK: mlt sp {{.*}} [0xed,0x7c]

in0 b,(0x12)
in0 a,(0x34)
out0 (0x56),c
out0 (0x78),a
; CHECK: in0 b, (18) {{.*}} [0xed,0x00,0x12]
; CHECK: in0 a, (52) {{.*}} [0xed,0x38,0x34]
; CHECK: out0 (86), c {{.*}} [0xed,0x09,0x56]
; CHECK: out0 (120), a {{.*}} [0xed,0x39,0x78]

tst b
tst a,c
tst a
tst (hl)
tst a,(hl)
tst 0x9a
tst a,0xbc
tstio 0xde
; CHECK: tst a, b {{.*}} [0xed,0x04]
; CHECK: tst a, c {{.*}} [0xed,0x0c]
; CHECK: tst a, a {{.*}} [0xed,0x3c]
; CHECK: tst a, (hl) {{.*}} [0xed,0x34]
; CHECK: tst a, (hl) {{.*}} [0xed,0x34]
; CHECK: tst a, 154 {{.*}} [0xed,0x64,0x9a]
; CHECK: tst a, 188 {{.*}} [0xed,0x64,0xbc]
; CHECK: tstio 222 {{.*}} [0xed,0x74,0xde]

otim
otdm
otimr
otdmr
; CHECK: otim {{.*}} [0xed,0x83]
; CHECK: otdm {{.*}} [0xed,0x8b]
; CHECK: otimr {{.*}} [0xed,0x93]
; CHECK: otdmr {{.*}} [0xed,0x9b]

slp
; CHECK: slp {{.*}} [0xed,0x76]
