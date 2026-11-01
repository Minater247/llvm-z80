; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/bitwise-ez80.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/bitwise-ez80.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/bitwise-ez80.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/bitwise-ez80.py %t.greedy-bank.s

target triple = "ez80"

define i24 @and24(i24 %x, i24 %y) {
  %r = and i24 %x, %y
  ret i24 %r
}

define i24 @or24(i24 %x, i24 %y) {
  %r = or i24 %x, %y
  ret i24 %r
}

define i24 @xor24(i24 %x, i24 %y) {
  %r = xor i24 %x, %y
  ret i24 %r
}

define i24 @not24(i24 %x) {
  %r = xor i24 %x, -1
  ret i24 %r
}

declare ptr @llvm.ptrmask.p0.i24(ptr, i24)

define ptr @mask_pointer(ptr %x, i24 %y) {
  %r = call ptr @llvm.ptrmask.p0.i24(ptr %x, i24 %y)
  ret ptr %r
}

; A dense byte switch needs a 24-bit mask when extending its table index.
define i8 @dispatch(i8 %index) {
  switch i8 %index, label %default [
    i8 0, label %zero
    i8 1, label %one
    i8 2, label %two
    i8 3, label %three
  ]
zero:
  ret i8 11
one:
  ret i8 23
two:
  ret i8 37
three:
  ret i8 41
default:
  ret i8 0
}
