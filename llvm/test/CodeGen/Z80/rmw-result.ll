; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/rmw-result.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/rmw-result.py %t.greedy.s
; A memory increment with a live result needs a load-only reload MMO.
target triple = "z80"
define i8 @increment(ptr %p) {
  %v = load i8, ptr %p
  %n = add i8 %v, 1
  store i8 %n, ptr %p
  ret i8 %n
}
define i8 @decrement(ptr %p) {
  %v = load i8, ptr %p
  %n = add i8 %v, -1
  store i8 %n, ptr %p
  ret i8 %n
}
