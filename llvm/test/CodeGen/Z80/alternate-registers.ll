; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=OFF
; RUN: llc -mattr=-alternate-regs -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=OFF
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ON
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=ez80 -mattr=+alternate-regs -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=ON
;
; Compiler-generated EXX/EX AF,AF' require environment permission, even for
; interrupt save/restore. The allocator must leave the OS's bank alone by
; default. Explicit assembly is outside this policy.
; OFF-LABEL: handler:
; OFF-NOT: exx
; OFF: push hl
; OFF-NOT: exx
; OFF: pop hl
; OFF-NOT: exx
; OFF: reti
; ON-LABEL: handler:
; ON: exx
; ON: inc hl
; ON: exx
; ON: reti

target triple = "z80"
@g = global i16 0
define preserve_allcc void @handler() "interrupt"="Generic" {
  %a = load volatile i16, ptr @g
  %b = add i16 %a, 1
  store volatile i16 %b, ptr @g
  ret void
}
