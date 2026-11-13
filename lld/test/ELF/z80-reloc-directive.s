# REQUIRES: z80
# RUN: llvm-mc -triple=z80 -filetype=obj %s -o %t.o
# RUN: llvm-readobj --relocations --hex-dump=.text %t.o | FileCheck %s --check-prefix=OBJECT
# RUN: ld.lld -m elf32z80 -e entry -Ttext=0x100 --defsym=address=0x1234 %t.o -o %t
# RUN: llvm-readobj --hex-dump=.text %t | FileCheck %s --check-prefix=LINK

.global entry
entry:
 .byte 0x18,0xaa
 .reloc entry+1,R_Z80_8_PCREL,next-1
 .byte 0xaa,0xaa
 .reloc entry+2,R_Z80_16_BE,address
 .byte 0xaa,0xaa,0xaa
 .reloc entry+4,BFD_RELOC_24,address+1
next:
 .byte 0xaa
 .reloc next,R_Z80_BYTE1,address
 .reloc next,BFD_RELOC_NONE,address

# .reloc preserves the initial bytes until the linker applies the relocation.
# OBJECT: 0x1 R_Z80_8_PCREL next 0xFFFFFFFF
# OBJECT: 0x2 R_Z80_16_BE address 0x0
# OBJECT: 0x4 R_Z80_24 address 0x1
# OBJECT: 0x7 R_Z80_BYTE1 address 0x0
# OBJECT: 0x7 R_Z80_NONE address 0x0
# OBJECT: 0x00000000 18aaaaaa aaaaaaaa
# LINK: 0x00000100 18051234 35120012
