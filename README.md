# LLVM/Clang targetting the Z80/EZ80

This is a fork of [harakas' version](https://github.com/harakas/llvm-z80), which is in turn a fork of [jacobly0's version](https://github.com/jacobly0/llvm-project).

The two main goals of this repository are to:
- Optimize the code generation and update the calling convention to make the code more efficient and smaller
- Stabilize the state of the patch to be more user-friendly and stable by:
    - Fixing some major bugs that would crash the program
    - Providing more informative error messages, since most developers would rather not dig through internal LLVM error messages for compile errors

## Tradeoffs to using this compiler
Pros:
- Function parameters are placed in registers. Almost every compiler other than harakas' patch uses the stack, which is incredibly inefficient. Keeping values in registers means that generated code is significantly less memory intensive for dense non-inlined function calls.
- LLVM provides significantly improved code inlining over other Z80 compilers. There has been a lot of development since 1998, and a compiler based in 2022 is a big leap.
- The compiler emits proper object files, meaning you can use standard linkers such as `binutils`, disassemblers, code analyzers, anything that reads ELF+Z80.

Cons:
- The ABI is currently not fully standard, as I am currently tweaking it to optimize the generated code. If you plan to call out to assembly functions, be aware of this, otherwise it shouldn't affect you.
- You have to use `__attribute__((reentrant))` on recursive functions, which is nonstandard. I am currently looking for a way around this so that existing recursive C remains portable.
- This is my first foray into working with LLVM, so things may break a bit more than other compilers. *Please* make an issue if you do find anything wrong!

## Installing

### Dependencies
Before installing, ensure you have the necessary dependencies.
```bash
# Ubuntu
sudo apt-get update && sudo apt-get -y install build-essential cmake ninja-build lzip
```

If your distro isn't listed here, the OSDev wiki has a great [guide](https://wiki.osdev.org/GCC_Cross-Compiler#Installing_Dependencies) on installing the dependencies. Just stop before the "Downloading the Source Code" section. I plan on expanding this more once I return home from university and have a spare laptop to run other distros on.

### Actually Installing It

```bash
# Optionally, you can set the PREFIX and BINUTILS_PREFIX environment variables to determine where the compiler and binutils are installed, respectively.

bash ./install.sh
```

## Example Code
```cpp
#include <stdint.h>

namespace ZX {
    struct Console {
        static void putchar(char c) {
            uint16_t iy = 23610;
            __asm__ ("rst $10" : "=a"(c) : "a"(c), "iy"(iy) : "h", "l", "d", "e", "b", "c", "cc", "memory");
        }

        static void at(uint8_t x, uint8_t y) {
            putchar(22);
            putchar(y);
            putchar(x);
        }

        static void print(const char *str) {
            while (*str)
                putchar(*str++);
        }
    };
};

int main()
{
  ZX::Console::at(10, 12);
  ZX::Console::print("Hello, world!");
  return 0;
}
```

### With Inlining
Assembly produced for the `int main()` function:

```
_main:
	ld	a, 22
	ld	iy, 23610
	rst $10
	ld	a, 12
	rst $10
	ld	a, 10
	rst $10
	ld	a, 72
	rst $10
	ld	a, 101
	rst $10
	ld	a, 108
	rst $10
	ld	a, 108
	rst $10
	ld	a, 111
	rst $10
	ld	a, 44
	rst $10
	ld	a, 32
	rst $10
	ld	a, 119
	rst $10
	ld	a, 111
	rst $10
	ld	a, 114
	rst $10
	ld	a, 108
	rst $10
	ld	a, 100
	rst $10
	ld	a, 33
	rst $10
	ld	hl, 0
	ld	de, 0
	ret
```

### No Inlining

Assembly produced for the `int main()` function:
```asm
_main:
	ld	e, 10
	ld	d, 12
	call	__ZN2ZX7Console2atEhh
	ld	de, _.str
	call	__ZN2ZX7Console5printEPKc
	ld	hl, 0
	ld	de, 0
	ret
```

Assembly produced for the `ZX::Console::print(const char*)` function:
```asm
__ZN2ZX7Console5printEPKc:
	ex	de, hl
	ld	a, (hl)
	or	a
	jr	z, .LBB2_3
	ld	iy, 23610
	inc	hl
	.local	.LBB2_2
.LBB2_2:
	ld	(__ZN2ZX7Console5printEPKc__variables), hl
	rst $10
	ld	hl, (__ZN2ZX7Console5printEPKc__variables)
	ld	a, (hl)
	inc	hl
	or	a
	jr	nz, .LBB2_2
	.local	.LBB2_3
.LBB2_3:
	ret
```