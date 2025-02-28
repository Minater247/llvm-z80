
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

all: sallie_gardner.tap

horse.h: process.sh
	bash process.sh

sallie_gardner.tap: sallie_gardner.hex
	python3 ../../utils/hex2tap.py sallie_gardner.hex --include-loader

sallie_gardner.hex: horse.h memory.ld main.o rle.o rle_decode.o
	$(LD) -T memory.ld -Map=sallie_gardner.map --oformat ihex main.o rle.o rle_decode.o ../../rtlib/librtlib.a -o sallie_gardner.hex

clean:
	rm -f *.o sallie_gardner sallie_gardner.hex sallie_gardner.tap sallie_gardner.map

distclean: clean
	rm -rf .depend horse_*.png horse_*.rle horse_*.bin bitmap cropped small horse.h

depend:
	$(CXX) $(CXXFLAGS) -MM *.cpp > .depend

-include .depend
