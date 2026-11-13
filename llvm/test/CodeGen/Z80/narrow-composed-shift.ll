; RUN: llc -stop-after=z80-postlegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/narrow-composed-shift.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/narrow-composed-shift.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/narrow-composed-shift.py %t.greedy.s
; RUN: llc -O0 -verify-machineinstrs %s -o %t.o0.s
; RUN: %python %S/Inputs/narrow-composed-shift.py %t.o0.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
;
; Legalizing a word OR exposes a low-byte use of the shift. The shift must
; still narrow after legalization, with no lost bits when its high lane is live.
target triple = "z80"

; CHECK-LABEL: name: composed
; CHECK: {{%[0-9]+}}:_(s8) = G_SHL
; CHECK-NOT: {{%[0-9]+}}:_(s16) = G_SHL
; CHECK: RET16
define i16 @composed(i8 %x, i8 %y) optsize minsize {
  %bit = trunc i8 %x to i1
  %wide = zext i1 %bit to i16
  %offset = shl i16 %wide, 3
  %low = zext i8 %y to i16
  %merged = or i16 %offset, %low
  %address = or i16 %merged, 256
  ret i16 %address
}

; CHECK-LABEL: name: high_lane
define i16 @high_lane(i8 %x, i8 %y) optsize minsize {
  %wide = zext i8 %x to i16
  %offset = shl i16 %wide, 3
  %low = zext i8 %y to i16
  %merged = or i16 %offset, %low
  %address = or i16 %merged, 256
  ret i16 %address
}

define i16 @byte_boundary(i8 %x, i8 %y) optsize minsize {
  %wide = zext i8 %x to i16
  %offset = shl i16 %wide, 8
  %low = zext i8 %y to i16
  %merged = or i16 %offset, %low
  ret i16 %merged
}

define i8 @variable_low(i16 %x, i8 %count) {
  %masked = and i8 %count, 7
  %wide = zext i8 %masked to i16
  %shift = shl i16 %x, %wide
  %low = trunc i16 %shift to i8
  ret i8 %low
}
