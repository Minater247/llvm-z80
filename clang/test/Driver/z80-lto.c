// REQUIRES: z80-registered-target, lld
// RUN: split-file %s %t
// RUN: %clang --target=z80 -O2 -ffreestanding -fintegrated-as -nostdlib -Wl,-e,entry,-Ttext=0x100,--gc-sections %t/entry.c %t/step.c -o %t/plain
// RUN: llvm-readobj -h --hex-dump=.text %t/plain | FileCheck %s --check-prefixes=Z80,PLAIN
// RUN: %clang --target=z80 -O2 -ffreestanding -fintegrated-as -nostdlib -flto -Wl,-e,entry,-Ttext=0x100,--gc-sections %t/entry.c %t/step.c -o %t/full
// RUN: llvm-readobj -h --hex-dump=.text %t/full | FileCheck %s --check-prefixes=Z80,LTO
// RUN: %clang --target=z80 -O2 -ffreestanding -fintegrated-as -nostdlib -flto=thin -Wl,-e,entry,-Ttext=0x100,--gc-sections %t/entry.c %t/step.c -o %t/thin
// RUN: llvm-readobj -h --hex-dump=.text %t/thin | FileCheck %s --check-prefixes=Z80,LTO
// RUN: %clang --target=ez80 -O2 -ffreestanding -fintegrated-as -nostdlib -flto -Wl,-e,entry,-Ttext=0x10000,--gc-sections %t/entry.c %t/step.c -o %t/adl
// RUN: llvm-readobj -h --hex-dump=.text %t/adl | FileCheck %s --check-prefix=ADL
// RUN: %clang --target=ez80-unknown-none-code16 -O2 -ffreestanding -fintegrated-as -nostdlib -flto=thin -Wl,-e,entry,-Ttext=0x100,--gc-sections %t/entry.c %t/step.c -o %t/code16
// RUN: llvm-readobj -h --hex-dump=.text %t/code16 | FileCheck %s --check-prefix=CODE16
// RUN: %clang --target=ez80 -O2 -ffreestanding -fintegrated-as -nostdlib -Wl,-e,entry,-Ttext=0x10000,--gc-sections %t/entry.c %t/step.c -o %t/adl-plain
// RUN: llvm-readobj -h --hex-dump=.text %t/adl-plain | FileCheck %s --check-prefix=ADL-PLAIN
// RUN: %clang --target=z80 -O2 -ffreestanding -fintegrated-as -c %t/step.c -o %t/step.o
// RUN: rm -f %t/native.a %t/bitcode.a
// RUN: llvm-ar rcs %t/native.a %t/step.o
// RUN: %clang --target=z80 -O2 -ffreestanding -fintegrated-as -nostdlib -flto -Wl,-e,entry,-Ttext=0x100,--gc-sections %t/entry.c %t/native.a -o %t/native-lib
// RUN: llvm-readobj -h --hex-dump=.text %t/native-lib | FileCheck %s --check-prefix=NATIVE-LIB
// RUN: %clang --target=z80 -O2 -ffreestanding -flto -c %t/step.c -o %t/step.bc
// RUN: llvm-ar rcs %t/bitcode.a %t/step.bc
// RUN: %clang --target=z80 -O2 -ffreestanding -fintegrated-as -nostdlib -Wl,-e,entry,-Ttext=0x100,--gc-sections %t/entry.c %t/bitcode.a -o %t/bitcode-lib
// RUN: llvm-readobj -h --hex-dump=.text %t/bitcode-lib | FileCheck %s --check-prefix=BITCODE-LIB

// Z80: Machine: EM_Z80
// Z80: Entry: 0x100
// Z80: Flags [ (0x1)
// PLAIN: Hex dump of section '.text':
// PLAIN-NEXT: 0x00000100 1e29c308 01767676 7b3cc9
// LTO: Hex dump of section '.text':
// LTO-NEXT: 0x00000100 3e2ac9
// ADL: Machine: EM_Z80
// ADL: Entry: 0x10000
// ADL: Flags [ (0x84)
// ADL: Hex dump of section '.text':
// ADL-NEXT: 0x00010000 dde53e2a dde1c9
// CODE16: Machine: EM_Z80
// CODE16: Entry: 0x100
// CODE16: Flags [ (0x4)
// CODE16: Hex dump of section '.text':
// CODE16-NEXT: 0x00000100 dde53e2a dde1c9
// ADL-PLAIN: Entry: 0x10000
// ADL-PLAIN: Flags [ (0x84)
// ADL-PLAIN: Hex dump of section '.text':
// ADL-PLAIN-NEXT: 0x00010000 dde5dd21 000000dd 39212900 00e5cd18
// ADL-PLAIN-NEXT: 0x00010010 0001ddf9 dde1c976 dde5dd21 000000dd
// NATIVE-LIB: Entry: 0x103
// NATIVE-LIB: Hex dump of section '.text':
// NATIVE-LIB-NEXT: 0x00000100 7b3cc91e 29c30001
// BITCODE-LIB: Entry: 0x100
// BITCODE-LIB: Hex dump of section '.text':
// BITCODE-LIB-NEXT: 0x00000100 1e29c305 017b3cc9

//--- entry.c
extern unsigned char step(unsigned char);
unsigned char entry(void) { return step(41); }

//--- step.c
unsigned char step(unsigned char x) { return x + 1; }
