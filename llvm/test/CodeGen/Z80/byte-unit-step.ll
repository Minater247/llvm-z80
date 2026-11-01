; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/byte-unit-step.py %t.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/byte-unit-step.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

define i8 @increment(i8 %value) {
; CHECK-LABEL: name: increment
; CHECK: INC8r
  %r = add i8 %value, 1
  ret i8 %r
}

define i8 @decrement(i8 %value) {
; CHECK-LABEL: name: decrement
; CHECK: DEC8r
  %r = add i8 %value, -1
  ret i8 %r
}

define i8 @commuted(i8 %value) {
; CHECK-LABEL: name: commuted
; CHECK: INC8r
  %r = add i8 1, %value
  ret i8 %r
}

define i8 @subtract_one(i8 %value) {
; CHECK-LABEL: name: subtract_one
; CHECK: DEC8r
  %r = sub i8 %value, 1
  ret i8 %r
}

define i8 @subtract_negative(i8 %value) {
; CHECK-LABEL: name: subtract_negative
; CHECK: INC8r
  %r = sub i8 %value, -1
  ret i8 %r
}

define i8 @increment_zero(i8 %value) {
; CHECK-LABEL: name: increment_zero
; CHECK: INC8r
  %r = add i8 %value, 1
  %zero = icmp eq i8 %r, 0
  %result = zext i1 %zero to i8
  ret i8 %result
}
