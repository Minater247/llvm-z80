# LLVM/Clang targetting Z80 / ZX Spectrum

This is a fork from https://github.com/jacobly0/llvm-project

I've been hacking it for use with the ZX Spectrum. Most changes have been done with speed in mind but I'm in no way an expert on compilers or llvm. This is just for fun.

Aim is to use C++ to program toy games or demos for the ZX Spectrum. I treat Z80 as a "microcontroller with more memory than usual", so use of heap, standard library, exceptions or even stack/`alloca` is not really something I plan to support.

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

<img width="323" alt="zx_hello_world" src="https://github.com/user-attachments/assets/6933252b-8e54-4606-98e1-158c67ca30f1" /><img width="322" alt="zx_hello_graphics" src="https://github.com/user-attachments/assets/20bdc17b-3fc0-40f8-a041-e7098a3fa28b" />

