// REQUIRES: z80-registered-target, lld
// RUN: split-file %s %t
// RUN: %python %S/Inputs/z80-callbacks-lto.py %clang %t

// Exercise native and bitcode archive extraction, indirect calls, constructor
// retention, section GC and fixed-address callback tables in all address modes.
// Direct and relocatable intermediate links must preserve the same contracts.
// The assembly runtime helpers are ordinary inputs to the Clang link command.

//--- entry.c
typedef unsigned char (*Callback)(unsigned char);
extern Callback const callbacks[2];
unsigned char selector, shared;
unsigned char entry(unsigned char x) { return callbacks[selector](x) + shared; }

typedef void (*Init)(void);
extern Init __init_array_start[], __init_array_end[];
void boot(void) {
  for (Init *p = __init_array_start; p != __init_array_end; ++p)
    (*p)();
}

//--- worker.c
extern unsigned char shared;
extern unsigned char startup_value(void);
static unsigned char increment(unsigned char x) { return x + 1; }
static unsigned char flip(unsigned char x) { return x ^ 128; }
unsigned char (*const callbacks[2])(unsigned char) = {increment, flip};
__attribute__((constructor(101))) static void initialize(void) {
  shared = startup_value();
}
unsigned char unused(void) { return 99; }

//--- runtime.S
.section .text._indcallhl,"ax",@progbits
.global _indcallhl
_indcallhl: jp (hl)
.section .text._indcallix,"ax",@progbits
.global _indcallix
_indcallix: jp (ix)
.section .text._indcall,"ax",@progbits
.global _indcall
_indcall: jp (iy)
.section .text.invoke,"ax",@progbits
.global invoke
invoke:
 ld a,d
 ld (selector),a
 jp entry
.section .text.observe,"ax",@progbits
.global observe
observe:
 ld a,(shared)
 ret
.section .text.startup_value,"ax",@progbits
.global startup_value
startup_value:
 ld a,17
 ret

//--- link.ld
ENTRY(invoke)
EXTERN(boot observe)
SECTIONS {
  .text code_base : { *(.text.invoke) *(.text*) }
  .init_array : {
    __init_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
    __init_array_end = .;
  }
  .callbacks callback_base : { KEEP(*(.rodata.callbacks)) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss callback_base + 0x100 (NOLOAD) : { *(.bss*) }
}
