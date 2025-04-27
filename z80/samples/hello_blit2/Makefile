
PREFIX ?= /opt/local/z80-none-elf

CXX=$(PREFIX)/bin/clang++
LD=$(PREFIX)/bin/z80-none-elf-ld
AS=$(PREFIX)/bin/z80-none-elf-as

ARCH = z80

CXXFLAGS += -target $(ARCH)-none-elf
# GAS: specify architecture. Full instruction set.
CXXFLAGS += -Wa,-march=$(ARCH)+full
# GAS: add small C compatibility -- so that "and a,10" works (otherwise only "and 10" is allowed)
CXXFLAGS += -Wa,-sdcc
# Disable standard headers
CXXFLAGS += -nostdinc
# Disable unwanted C++ crap -- rtti and exceptions
CXXFLAGS += -fno-rtti -fno-exceptions -ffunction-sections -fdata-sections
# Optimize!
CXXFLAGS += -O3
# Enable warnings
CXXFLAGS += -Wall
# We use c++20
CXXFLAGS += -std=c++20

CXXFLAGS += -I../../include

ASFLAGS += -march=$(ARCH)+full -sdcc

all: hello_blit2.tap

hello_blit2.tap: hello_blit2.hex
	python3 ../../utils/hex2tap.py hello_blit2.hex --include-loader

hello_blit2.hex: memory.ld main.o
	$(LD) -T memory.ld -Map=hello_blit2.map --oformat ihex main.o ../../rtlib/librtlib.a -o hello_blit2.hex

clean:
	rm -f *.o hello_blit2 hello_blit2.hex hello_blit2.tap hello_blit2.map

distclean: clean
	rm -f .depend

depend:
	$(CXX) $(CXXFLAGS) -MM *.cpp > .depend

-include .depend
