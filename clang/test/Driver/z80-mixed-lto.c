// REQUIRES: z80-registered-target, lld
// RUN: split-file %s %t
// RUN: %clang --target=z80 -O2 -ffreestanding -nostdlib -Wl,-e,entry,-Ttext=0x100 %t/entry.S %t/worker.c -o %t/plain
// RUN: llvm-readobj -h --hex-dump=.text %t/plain | FileCheck %s
// RUN: %clang --target=z80 -O2 -ffreestanding -nostdlib -flto -Wl,-e,entry,-Ttext=0x100 %t/entry.S %t/worker.c -o %t/full
// RUN: llvm-readobj -h --hex-dump=.text %t/full | FileCheck %s
// RUN: %clang --target=z80 -O2 -ffreestanding -nostdlib -flto=thin -Wl,-e,entry,-Ttext=0x100 %t/entry.S %t/worker.c -o %t/thin
// RUN: llvm-readobj -h --hex-dump=.text %t/thin | FileCheck %s
// CHECK: Machine: EM_Z80
// CHECK: Entry: 0x100
// CHECK: Flags [ (0x1)
// CHECK: Hex dump of section '.text':
// CHECK-NEXT: 0x00000100 1e29c308 01767676 7b073cc9

//--- entry.S
#define INPUT 41
.text
.global entry
entry:
 ld e,INPUT
 jp worker

//--- worker.c
unsigned char worker(unsigned char x) {
  __asm__("rlca" : "+a"(x) : : "cc");
  return x + 1;
}
