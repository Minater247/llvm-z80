// REQUIRES: z80-registered-target, lld
// RUN: split-file %s %t
// RUN: %clang --target=z80 -O2 -g -malternate-registers -ffreestanding -nostdlib -Wl,--gc-sections -T %t/link.ld %t/start.S %t/worker.c -o %t/plain
// RUN: llvm-readobj -h --symbols --hex-dump=.vectors %t/plain | FileCheck %s
// RUN: llvm-dwarfdump --verify %t/plain | FileCheck %s --check-prefix=DEBUG
// RUN: %clang --target=z80 -O2 -g -malternate-registers -ffreestanding -nostdlib -flto -Wl,--gc-sections -T %t/link.ld %t/start.S %t/worker.c -o %t/full
// RUN: llvm-readobj -h --symbols --hex-dump=.vectors %t/full | FileCheck %s
// RUN: llvm-dwarfdump --verify %t/full | FileCheck %s --check-prefix=DEBUG
// RUN: %clang --target=z80 -O2 -g -malternate-registers -ffreestanding -nostdlib -flto=thin -Wl,--gc-sections -T %t/link.ld %t/start.S %t/worker.c -o %t/thin
// RUN: llvm-readobj -h --symbols --hex-dump=.vectors %t/thin | FileCheck %s
// RUN: llvm-dwarfdump --verify %t/thin | FileCheck %s --check-prefix=DEBUG
// RUN: %clang --target=z80 -O2 -flto -c %t/override.c -o %t/override.o
// RUN: rm -f %t/override.a
// RUN: llvm-ar rcs %t/override.a %t/override.o
// RUN: %clang --target=z80 -O2 -g -malternate-registers -ffreestanding -nostdlib -flto -DOVERRIDE_TICK -Wl,--gc-sections -T %t/link.ld %t/start.S %t/worker.c %t/override.a -o %t/override
// RUN: llvm-readobj -h --symbols --hex-dump=.vectors %t/override | FileCheck %s --implicit-check-not="Name: default_tick"
// RUN: %clang --target=z80 -O2 -flto=thin -c %t/override.c -o %t/override.o
// RUN: llvm-ar rcs %t/override.a %t/override.o
// RUN: %clang --target=z80 -O2 -g -malternate-registers -ffreestanding -nostdlib -flto=thin -DOVERRIDE_TICK -Wl,--gc-sections -T %t/link.ld %t/start.S %t/worker.c %t/override.a -o %t/override-thin
// RUN: llvm-readobj -h --symbols --hex-dump=.vectors %t/override-thin | FileCheck %s --implicit-check-not="Name: default_tick"

// Assembly references retain the C entry points and externally accessed state
// through LTO. The linker script fixes the reset/interrupt vectors and state.
// DEBUG: No errors.
// CHECK: Machine: EM_Z80
// CHECK: Entry: 0x0
// CHECK: Name: state
// CHECK-NEXT: Value: 0x1100
// CHECK: Hex dump of section '.vectors':
// CHECK-NEXT: 0x00000000 f3310018 ed56c300 01
// CHECK: 0x00000030 00000000 00000000 f5c5d5e5 dde5fde5
// CHECK-NEXT: 0x00000040 08f508d9 c5d5e5d9 cd{{[0-9a-f]+}}d9 e1d1c1d9
// CHECK-NEXT: 0x00000050 08f108fd e1dde1e1 d1c1f1fb ed4d

//--- link.ld
OUTPUT_FORMAT(elf32-z80)
ENTRY(reset)
SECTIONS {
  .vectors 0 : { KEEP(*(.vectors)) }
  .text 0x100 : { *(.text.start) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss 0x1100 (NOLOAD) : { *(.bss*) }
}

//--- start.S
.section .vectors,"ax",@progbits
.global reset
reset:
 di
 ld sp,0x1800
 im 1
 jp boot
.org 0x38
 push af
 push bc
 push de
 push hl
 push ix
 push iy
 ex af,af'
 push af
 ex af,af'
 exx
 push bc
 push de
 push hl
 exx
 call tick
 exx
 pop hl
 pop de
 pop bc
 exx
 ex af,af'
 pop af
 ex af,af'
 pop iy
 pop ix
 pop hl
 pop de
 pop bc
 pop af
 ei
 reti
.section .text.start,"ax",@progbits
boot:
#ifdef OVERRIDE_TICK
 call override_token
#endif
 ld a,17
 ld (state),a
 ld bc,0x1234
 ld de,0x5678
 ld hl,0x9abc
 exx
 ld bc,0x1357
 ld de,0x2468
 ld hl,0xabcd
 ld ix,0x3210
 ld iy,0x7654
 ex af,af'
 ld a,0x81
 scf
 ex af,af'
 ei
idle:
 nop
 jr idle
.global invoke
invoke:
 call worker
 ld (state),a
 ret

//--- worker.c
unsigned char state;
unsigned char worker(unsigned char x) {
  __asm__("rlca" : "+a"(x) : : "cc");
  return x + 1;
}
void default_tick(void) { ++state; }
void tick(void) __attribute__((weak, alias("default_tick")));

//--- override.c
extern unsigned char state;
void tick(void) { state += 2; }
// This reference extracts the archive member containing the strong handler.
void override_token(void) {}
