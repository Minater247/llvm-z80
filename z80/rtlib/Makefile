
PREFIX ?= /opt/local/ez80-none-elf

LIBDIR = $(PREFIX)/lib
BINDIR = $(PREFIX)/bin

AR = $(BINDIR)/z80-none-elf-ar
AS = $(BINDIR)/z80-none-elf-as

ARCH = z80

ASFLAGS += -march=$(ARCH)+full -sdcc

SRC = frameset.s memcpy.s
OBJS = $(SRC:.s=.o)
LIB = librtlib.a

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs -o $@ $^

clean:
	rm -f *.o *.a

.PHONY: all clean
