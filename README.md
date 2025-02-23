# LLVM/Clang targetting Z80 / ZX Spectrum

This is a branch I cloned from https://github.com/jacobly0/llvm-project

I've been hacking it for use with the ZX Spectrum. Most changes have been done with speed in mind but I'm in no way an expert on compilers or llvm. This is just for fun.

Aim is to use C++ to program toy games or demos for the ZX Spectrum. I treat Z80 as a "microcontroller with more memory than usual", so use of heap, standard library, exceptions or even stack/`alloca` is not really something I plan to support.

How to build:

```
$ sudo apt-get update && sudo apt-get -y install cmake ninja-build lzip
$ bash install.sh
```

This will download and install binutils and compile clang. The default installation directory is `/opt/local/z80-none-elf`.

There are some samples under z80/samples. Here's how you'd compile and run one under the fuse-gtk emulator:

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

This is all the help you'll get for me with this. Have fun.
