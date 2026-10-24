; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/bit-conditions.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/bit-conditions.py %t.greedy.s
target triple = "z80"
define i8 @bit_zero(ptr %p) {
  %v = load volatile i8, ptr %p
  %b = and i8 %v, 4
  %c = icmp eq i8 %b, 0
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @bit_nonzero(ptr %p) {
  %v = load volatile i8, ptr %p
  %b = and i8 %v, 128
  %c = icmp ne i8 %b, 0
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @negative(i8 %v) {
  %c = icmp slt i8 %v, 0
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @nonnegative(ptr %p) {
  %v = load volatile i8, ptr %p
  %c = icmp sge i8 %v, 0
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @negative_sle(i8 %v) {
  %c = icmp sle i8 %v, -1
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @nonnegative_sgt(i8 %v) {
  %c = icmp sgt i8 %v, -1
  %r = zext i1 %c to i8
  ret i8 %r
}

define i8 @bit_before_store(ptr %p) {
  %v = load volatile i8, ptr %p
  store volatile i8 0, ptr %p
  %b = and i8 %v, 4
  %c = icmp ne i8 %b, 0
  %r = zext i1 %c to i8
  ret i8 %r
}
define i8 @sign_before_store(ptr %p) {
  %v = load volatile i8, ptr %p
  store volatile i8 0, ptr %p
  %c = icmp slt i8 %v, 0
  %r = zext i1 %c to i8
  ret i8 %r
}
