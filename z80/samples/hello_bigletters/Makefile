
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

ASFLAGS += -march=$(ARCH)+full -sdcc

all: hello_bigletters.tap

hello_bigletters.tap: hello_bigletters.hex
	python3 ../../utils/hex2tap.py hello_bigletters.hex --include-loader

hello_bigletters.hex: memory.ld main.o
	$(LD) -T memory.ld -Map=hello_bigletters.map --oformat ihex main.o -o hello_bigletters.hex

clean:
	rm -f *.o hello_bigletters hello_bigletters.hex hello_bigletters.tap hello_bigletters.map

distclean: clean
	rm -f .depend

depend:
	$(CXX) $(CXXFLAGS) -MM *.cpp > .depend

-include .depend
