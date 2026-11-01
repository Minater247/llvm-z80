; RUN: %python %S/Inputs/byte-division.py --emit %s %t.ll
; RUN: llc -verify-machineinstrs %t.ll -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/byte-division.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %t.ll -o %t.greedy.s
; RUN: %python %S/Inputs/byte-division.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %t.ll -o %t.bank.s
; RUN: %python %S/Inputs/byte-division.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %t.ll -o %t.greedy-bank.s
; RUN: %python %S/Inputs/byte-division.py %t.greedy-bank.s
; RUN: llc -mcpu=z180 -verify-machineinstrs %t.ll -o /dev/null
;
; Exercise every byte numerator for each divisor lowered without a call.
; The reconstruction must match both operands before reusing a remainder.
; CHECK-LABEL: reconstruct:
; CHECK-NOT: call
; CHECK: sub{{[ \t]+}}12
; CHECK-NOT: sub{{[ \t]+}}12
; CHECK: add{{[ \t]+}}a, 12
; CHECK-NOT: call
; CHECK: ret
; CHECK-LABEL: different_factor:
; CHECK: ret

target triple = "z80"

define i16 @reconstruct(i8 %x) nounwind minsize {
  %q = udiv i8 %x, 12
  %product = mul i8 %q, 12
  %r = sub i8 %x, %product
  %qw = zext i8 %q to i16
  %rw = zext i8 %r to i16
  %hi = shl i16 %rw, 8
  %both = or i16 %qw, %hi
  ret i16 %both
}

define i8 @different_factor(i8 %x) nounwind minsize {
  %q = udiv i8 %x, 12
  %product = mul i8 %q, 14
  %r = sub i8 %x, %product
  ret i8 %r
}

define i8 @different_source(i8 %x, i8 %y) nounwind minsize {
  %q = udiv i8 %y, 12
  %product = mul i8 %q, 12
  %r = sub i8 %x, %product
  ret i8 %r
}

define i8 @conditional(i8 %x, i8 %y) nounwind {
  %small = icmp ult i8 %x, 128
  br i1 %small, label %left, label %right
left:
  %a = udiv i8 %x, 12
  br label %exit
right:
  %b = urem i8 %y, 17
  br label %exit
exit:
  %r = phi i8 [ %a, %left ], [ %b, %right ]
  %sum = add i8 %r, %x
  ret i8 %sum
}
