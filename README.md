# LLVM/Clang targetting Z80 / ZX Spectrum

This is a fork from https://github.com/jacobly0/llvm-project

I've been hacking it for use with the ZX Spectrum. Most changes have been done with speed in mind but I'm in no way an expert on compilers or llvm. This is just for fun.

Aim is to use C++ to program toy games or demos for the ZX Spectrum. I treat Z80 as a "microcontroller with more memory than usual", so use of heap, standard library, exceptions or even stack/`alloca` is not really something I plan to support.

Main changes:
* function arguments are passed using registers instead of stack
* inlining of various bit operations
* spilled variables are placed into a static global variable instead of stack
* functions that want to use recursion must have `__attribute__((reentrant))`
* small memcopies are inlined a lot more
* memcopies with known length are optimized to a `CALL` to `__memcpyNN` where `NN` is modulo 32 of the length
* sequential batches of LDI statements are optimized to speed up sprite blitting
* frame pointer setup is inlined (for reentrant functions)

How to build:

```
$ sudo apt-get update && sudo apt-get -y install cmake ninja-build lzip
$ bash install.sh
```

This will download and install binutils and compile clang. The default installation directory is `/opt/local/z80-none-elf`.

There are some samples under z80/samples. Here's how you'd compile and run one under the `fuse-gtk` emulator:

```bash
$ cd z80/samples/hello_world/
$ make
/opt/local/z80-none-elf/bin/clang++ -target z80-none-elf -Wa,-march=z80+full -Wa,-sdcc -nostdinc -fno-rtti -fno-exceptions -ffunction-sections -fdata-sections -O3 -Wall -std=c++20   -c -o main.o main.cpp
/tmp/main-76f055.s: Assembler messages:
/tmp/main-76f055.s:98: Warning: unrecognized section type
/opt/local/z80-none-elf/bin/z80-none-elf-ld -T memory.ld -Map=main.map --oformat ihex main.o -o main.hex
python3 ../../utils/hex2tap.py main.hex --include-loader
$ fuse-gtk main.tap
```

The compiler is not really that stable. It breaks and crashes on lots of various code. But for some it works.

This is all the help you'll get from me with this. Have fun.

Example application:
```c++
namespace ZX {
    using int8_t = char;
    using uint8_t = unsigned char;
    using int16_t = int;
    using uint16_t = unsigned int;

    struct Console {
        static void putchar(char c) {
            uint16_t iy = 23610;
            __asm__ ("rst $10" : "=a"(c) : "a"(c), "iy"(iy) : "h", "l", "d", "e", "b", "c", "cc", "memory");
        }

        static void at(uint8_t x, uint8_t y) __attribute__((noinline)) {
            putchar(22);
            putchar(y);
            putchar(x);
        }

        static void print(const char *str) __attribute__((noinline)) {
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

<img width="323" alt="zx_hello_world" src="https://github.com/user-attachments/assets/6933252b-8e54-4606-98e1-158c67ca30f1" /><img width="322" alt="zx_hello_graphics" src="https://github.com/user-attachments/assets/20bdc17b-3fc0-40f8-a041-e7098a3fa28b" /><img width="323" alt="zx3" src="https://github.com/user-attachments/assets/fc9bd2ae-b9dd-402e-9939-d9a5028b0a59" />

Assembly produced for the `int main()` function:
```gas
_main:
        ld      l, 10
        ld      h, 12
        call    __ZN2ZX7Console2atEhh
        ld      hl, _.str
        call    __ZN2ZX7Console5printEPKc
        ld      hl, 0
        ret
```

Assembly produced for the `ZX::Console::print(const char*)` function:
```gas
__ZN2ZX7Console5printEPKc:
        ld      e, l
        ld      d, h
        ld      c, (hl)
        ld      a, c
        or      a, a
        jr      z, .LBB2_3
        ld      iy, 23610
        inc     de
        ld      (__ZN2ZX7Console5printEPKc__variables), de
        .local  .LBB2_2
.LBB2_2:
        ld      a, c
        ;APP
        rst $10
        ;NO_APP
        ld      hl, (__ZN2ZX7Console5printEPKc__variables)
        ld      c, (hl)
        inc     hl
        ld      (__ZN2ZX7Console5printEPKc__variables), hl
        ld      a, c
        or      a, a
        jr      nz, .LBB2_2
        .local  .LBB2_3
.LBB2_3:
        ret
```

