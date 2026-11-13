// REQUIRES: z80-registered-target
// RUN: %clang_cc1 -triple z80 -O2 -emit-obj %s -o %t.z80.o
// RUN: llvm-readobj --file-headers --relocations %t.z80.o | FileCheck %s --check-prefix=Z80
// RUN: %clang_cc1 -triple ez80 -O2 -emit-obj %s -o %t.ez80.o
// RUN: llvm-readobj --file-headers --relocations %t.ez80.o | FileCheck %s --check-prefix=EZ80
// RUN: %clang_cc1 -triple ez80-unknown-none-code16 -O2 -emit-obj %s -o %t.code16.o
// RUN: llvm-readobj --file-headers --relocations %t.code16.o | FileCheck %s --check-prefix=CODE16

extern unsigned char state;
unsigned char *state_pointer = &state;

unsigned char read_state(void) {
  return state;
}

// Z80: Format: elf32-z80
// Z80: Flags [ (0x1)
// Z80: R_Z80_16 state
// Z80: R_Z80_16 state

// EZ80: Format: elf32-z80
// EZ80: Flags [ (0x84)
// EZ80: R_Z80_24 state
// EZ80: R_Z80_24 state

// CODE16: Format: elf32-z80
// CODE16: Flags [ (0x4)
// CODE16: R_Z80_16 state
// CODE16: R_Z80_16 state
