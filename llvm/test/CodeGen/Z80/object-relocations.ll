; RUN: llc -mtriple=z80 -filetype=obj %s -o %t.z80.o
; RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.z80.o | FileCheck %s --check-prefix=Z80
; RUN: llc -mtriple=ez80 -filetype=obj %s -o %t.ez80.o
; RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.ez80.o | FileCheck %s --check-prefix=EZ80
; RUN: llc -mtriple=ez80-unknown-none-code16 -filetype=obj %s -o %t.code16.o
; RUN: llvm-readobj --file-headers --relocations %t.code16.o | FileCheck %s --check-prefix=CODE16

; RUN: obj2yaml %t.ez80.o | yaml2obj -o %t.roundtrip.o
; RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.roundtrip.o | FileCheck %s --check-prefix=EZ80

@external = external global i8
@pointer = global ptr @external, align 1
@offset = global ptr getelementptr (i8, ptr @external, i16 7), align 1
@zero = global [13 x i8] zeroinitializer, align 4
@private = internal global i16 4660, align 2
@weak = weak global i8 3

; Z80: Format: elf32-z80
; Z80: Machine: EM_Z80
; Z80: Flags [ (0x1)
; Z80: Name: .bss
; Z80: Type: SHT_NOBITS
; Z80: Size: 13
; Z80: AddressAlignment: 4
; Z80: R_Z80_16 external 0x0
; Z80: R_Z80_16 external 0x7
; Z80: Name: private
; Z80: Binding: Local
; Z80: Name: pointer
; Z80: Binding: Global
; Z80: Name: weak
; Z80: Binding: Weak

; EZ80: Format: elf32-z80
; EZ80: Machine: EM_Z80
; EZ80: Flags [ (0x84)
; EZ80: Name: .data
; EZ80: Size: 9
; EZ80: R_Z80_24 external 0x0
; EZ80: R_Z80_24 external 0x7
; EZ80: Name: weak
; EZ80: Binding: Weak

; CODE16: Flags [ (0x4)
; CODE16: R_Z80_16 external 0x0
; CODE16: R_Z80_16 external 0x7
