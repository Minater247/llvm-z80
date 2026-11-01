; RUN: llc -stop-after=irtranslator -verify-machineinstrs %s -o - | FileCheck %s --implicit-check-not=G_SHL
; RUN: llc -mtriple=ez80 -stop-after=irtranslator -verify-machineinstrs %s -o - | FileCheck %s --implicit-check-not=G_SHL
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=EZ80
; RUN: llc -jump-table-density=100 -verify-machineinstrs %s -o %t.custom.s
; RUN: %python %S/Inputs/switch-membership.py %t.custom.s
; RUN: llc -jump-table-density=100 -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/switch-membership.py %t.greedy.s
; RUN: llc -jump-table-density=100 -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/switch-membership.py %t.bank.s
; RUN: llc -jump-table-density=100 -mattr=+alternate-regs -regalloc=greedy -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/switch-membership.py %t.greedy-bank.s

; Switch lowering must account for variable shifts expanding into sequences.
; Sparse sets use comparisons or jump tables instead of variable-shift bitmaps.
; Execute the comparison form exhaustively and check default jump-table selection.
target triple = "z80"

; EZ80-LABEL: .LJTI0_0:
; EZ80-NEXT: d24 .LBB0_2
; EZ80-NEXT: d24 .LBB0_3

define i8 @byte_set(i8 %value) {
; CHECK-LABEL: name: byte_set
; CHECK: G_ICMP
; CHECK: G_JUMP_TABLE
  switch i8 %value, label %miss [
    i8 1, label %hit
    i8 4, label %hit
    i8 6, label %hit
    i8 9, label %hit
    i8 12, label %hit
    i8 14, label %hit
  ]
hit:
  ret i8 1
miss:
  ret i8 0
}

define i8 @word_set(i16 %value) {
; CHECK-LABEL: name: word_set
; CHECK: G_ICMP
; CHECK: G_JUMP_TABLE
  switch i16 %value, label %miss [
    i16 -4, label %hit
    i16 -1, label %hit
    i16 2, label %hit
    i16 5, label %hit
    i16 8, label %hit
    i16 10, label %hit
  ]
hit:
  ret i8 1
miss:
  ret i8 0
}
