; RUN: llc -stop-after=z80-prelegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/shared-record-base.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/shared-record-base.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/shared-record-base.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

; Preserve the common adjusted base: a combined offset above 127 cannot be
; encoded in the load/store. Negative displacements and both endpoints work.
; MIR-LABEL: name: fields
; MIR: %[[K:[0-9]+]]:_(s16) = G_CONSTANT i16 272
; MIR: %[[BASE:[0-9]+]]:_(p0) = G_PTR_ADD %{{[0-9]+}}, %[[K]]
; MIR: G_PTR_ADD %[[BASE]],
; MIR: G_PTR_ADD %[[BASE]],
; MIR: G_PTR_ADD %[[BASE]],
define i8 @fields(ptr %p, i8 %v) optsize {
  %base = getelementptr i8, ptr %p, i16 272
  %a = getelementptr i8, ptr %base, i16 -128
  %b = getelementptr i8, ptr %base, i16 56
  %c = getelementptr i8, ptr %base, i16 127
  store volatile i8 %v, ptr %a
  %x = load volatile i8, ptr %b
  %r = xor i8 %x, %v
  store volatile i8 %r, ptr %c
  ret i8 %r
}
