; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/wide-arithmetic.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/wide-arithmetic.py %t.greedy.s

target triple = "z80"
define i16 @sub_shift(i16 %a, i16 %b) {
 %d = sub i16 %a, %b
 %s = shl i16 %d, 2
 ret i16 %s
}
define i8 @high_shift(i16 %a) {
 %s = lshr i16 %a, 8
 %b = trunc i16 %s to i8
 %r = lshr i8 %b, 1
 ret i8 %r
}
define i32 @combine(i32 %a, i32 %b) {
 %s = shl i32 %a, 13
 %t = lshr i32 %b, 7
 %r = or i32 %s, %t
 ret i32 %r
}
define i16 @roundtrip(i16 %a, i16 %b) {
 %x = zext i16 %a to i32
 %y = zext i16 %b to i32
 %r = call i32 @combine(i32 %x,i32 %y)
 %hi = lshr i32 %r, 16
 %v = trunc i32 %hi to i16
 ret i16 %v
}

define i16 @two_conditions(i16 %x) {
 %zero = icmp eq i16 %x, 0
 %last = icmp eq i16 %x, 63
 %a = zext i1 %last to i16
 %b = add i16 %a, 1
 %r = select i1 %zero, i16 0, i16 %b
 ret i16 %r
}
define i8 @both_ranges(i16 %x, i16 %y) {
 %a = icmp ult i16 %x, 6
 %b = icmp ult i16 %y, 2048
 %c = and i1 %a, %b
 %r = sext i1 %c to i8
 ret i8 %r
}
