; RUN: llc -verify-machineinstrs -stop-after=instruction-select %s -o - | FileCheck %s
; RUN: %python %S/Inputs/scheduled-byte-masks.py llc %s %t.dir
; Carry-derived masks have no dependency on the accumulator's previous value.
; Import ABI inputs before address arithmetic needs their physical pairs.
target triple = "z80"

define i8 @less_mask(i8 %x, i8 %y) nounwind {
; CHECK-LABEL: name: less_mask
; CHECK: SBC8aa implicit-def $a, implicit-def $f, implicit $f
  %test = icmp ult i8 %x, 160
  %mask = sext i1 %test to i8
  %result = or i8 %mask, %y
  ret i8 %result
}

define i8 @greater_mask(i8 %x, i8 %y) nounwind {
  %test = icmp uge i8 %x, 160
  %mask = sext i1 %test to i8
  %result = or i8 %mask, %y
  ret i8 %result
}

define i8 @sign_mask(i8 %x) nounwind {
  %sign = ashr i8 %x, 7
  ret i8 %sign
}

define i8 @extended_bit(i8 %x) nounwind {
  %bit = trunc i8 %x to i1
  %mask = sext i1 %bit to i8
  ret i8 %mask
}

define void @input_pressure(i8 %index, i8 %threshold, i8 %low, i8 %high) nounwind {
  %wide = zext i8 %index to i16
  %offset = shl i16 %wide, 3
  %address = add i16 %offset, 8704
  %pointer = inttoptr i16 %address to ptr
  %old = load volatile i8, ptr %pointer
  %test = icmp uge i8 %threshold, %old
  br i1 %test, label %write, label %exit
write:
  %next = getelementptr i8, ptr %pointer, i16 2
  store volatile i8 %low, ptr %next
  %next2 = getelementptr i8, ptr %pointer, i16 3
  store volatile i8 %high, ptr %next2
  br label %exit
exit:
  ret void
}
