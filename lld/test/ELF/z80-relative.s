# REQUIRES: z80
# RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
# RUN: ld.lld -Ttext=0 %t.o -o %t
# RUN: llvm-readobj --hex-dump=.text %t | FileCheck %s
# RUN: not ld.lld -Ttext=0x100 %t.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=RANGE

.space 16
.global _start
_start:
 jr 0
 jr $-2
# CHECK: 0x00000010 18ee18fc
# RANGE: relocation R_Z80_8_PCREL out of range: -274 is not in [-128, 127]
