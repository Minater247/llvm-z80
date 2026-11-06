; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/bitfield-merge.py llc %s %t.dir
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

; Merge complementary fields without keeping two masked temporaries.
; CHECK-LABEL: nibbles:
; CHECK: xor
; CHECK-NEXT: and
; CHECK-NEXT: xor
; CHECK-NOT: and
; CHECK: ret
define i8 @nibbles(i8 %old, i8 %new) nounwind {
  %low = and i8 %old, 15
  %high = and i8 %new, -16
  %r = or i8 %low, %high
  ret i8 %r
}

; Both source bytes remain observable after computing the merged value.
define i8 @preserved(i8 %old, i8 %new) nounwind {
  %low = and i8 %old, 15
  %high = and i8 %new, -16
  %r = or i8 %high, %low
  store volatile i8 %old, ptr inttoptr (i16 20480 to ptr)
  store volatile i8 %new, ptr inttoptr (i16 20481 to ptr)
  ret i8 %r
}

; A shared mask retains its other consumer.
define i8 @shared_mask(i8 %old, i8 %new) nounwind {
  %low = and i8 %old, 15
  %high = and i8 %new, -16
  store volatile i8 %low, ptr inttoptr (i16 20480 to ptr)
  %r = or i8 %high, %low
  ret i8 %r
}

; Overlapping fields require the ordinary OR semantics.
define i8 @overlapping(i8 %old, i8 %new) nounwind {
  %low = and i8 %old, 15
  %high = and i8 %new, 31
  %r = or i8 %high, %low
  ret i8 %r
}
