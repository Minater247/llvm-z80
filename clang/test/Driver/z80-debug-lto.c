// REQUIRES: z80-registered-target, lld
// RUN: split-file %s %t
// RUN: %clang --target=ez80 -O2 -g -gdwarf-4 -funwind-tables -ffreestanding -c %t/entry.c -o %t/entry.o
// RUN: llvm-dwarfdump --verify %t/entry.o | FileCheck %s --check-prefix=VERIFY
// RUN: llvm-dwarfdump --debug-info --eh-frame %t/entry.o | FileCheck %s --check-prefix=OBJECT
// RUN: %clang --target=ez80 -O2 -g -gdwarf-4 -funwind-tables -ffreestanding -nostdlib -Wl,-e,entry,-Ttext=0x123400,-Tbss=0x234500 %t/entry.c %t/step.c -o %t/plain
// RUN: llvm-dwarfdump --verify %t/plain | FileCheck %s --check-prefix=VERIFY
// RUN: llvm-dwarfdump --debug-info --debug-line --eh-frame %t/plain | FileCheck %s
// RUN: %clang --target=ez80 -O2 -g -gdwarf-4 -funwind-tables -ffreestanding -nostdlib -flto -Wl,-e,entry,-Ttext=0x123400,-Tbss=0x234500 %t/entry.c %t/step.c -o %t/full
// RUN: llvm-dwarfdump --verify %t/full | FileCheck %s --check-prefix=VERIFY
// RUN: llvm-dwarfdump --debug-info --debug-line --eh-frame %t/full | FileCheck %s
// RUN: %clang --target=ez80 -O2 -g -gdwarf-5 -funwind-tables -ffreestanding -nostdlib -flto=thin -Wl,-e,entry,-Ttext=0x123400,-Tbss=0x234500 %t/entry.c %t/step.c -o %t/thin
// RUN: llvm-dwarfdump --verify %t/thin | FileCheck %s --check-prefix=VERIFY
// RUN: llvm-dwarfdump --debug-info --debug-line --eh-frame %t/thin | FileCheck %s
// RUN: llvm-dwarfdump --debug-addr %t/thin | FileCheck %s --check-prefix=ADDR
// RUN: %clang --target=ez80 -O2 -g -gdwarf-aranges -ffreestanding -c %t/entry.c -o %t/aranges.o
// RUN: llvm-dwarfdump --debug-aranges %t/aranges.o | FileCheck %s --check-prefix=ARANGES

// VERIFY: No errors.
// ADDR: addr_size = 0x03
// ADDR: 0x234500
// ARANGES: addr_size = 0x03
// ARANGES: [0x000000,
// OBJECT: addr_size = 0x03
// OBJECT: DW_AT_name{{.*}}("shared")
// OBJECT: Return address column: 8
// OBJECT: DW_CFA_offset: IX -6
// CHECK: addr_size = 0x03
// CHECK: DW_AT_name{{.*}}("shared")
// CHECK: DW_AT_location{{.*}}DW_OP_addr{{x?}} {{.*}}
// CHECK: Return address column: 8
// CHECK: FDE cie={{.*}}pc=00123400...
// CHECK: DW_CFA_offset: IX -6
// CHECK: .debug_line contents:
// CHECK: 0x0000000000123400

//--- entry.c
extern unsigned char step(unsigned char);
unsigned char shared __attribute__((used));
unsigned char entry(unsigned char x) {
  volatile unsigned char slot = x;
  shared = slot;
  return step(shared) + 1;
}

//--- step.c
__attribute__((noinline)) unsigned char step(unsigned char x) {
  return x ^ 42;
}
