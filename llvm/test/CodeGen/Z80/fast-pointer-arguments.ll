; RUN: llc -mtriple=ez80-unknown-unknown-code16 -verify-machineinstrs %s -o /dev/null
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/fast-pointer-arguments.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/fast-pointer-arguments.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/fast-pointer-arguments.py %t.greedy.s

target triple = "z80"

; Pointer arguments enter IY while integer arguments retain DE/BC/HL.
; The large frame must preserve all four incoming pairs during SP setup.
; CHECK-LABEL: large:
; CHECK: push ix
; CHECK: push hl
; CHECK: ld l, (ix - 2)
; CHECK: ld h, (ix - 1)
define fastcc i16 @large(ptr %p, i16 %x, i16 %y, i16 %z) noinline optsize {
  %a = alloca [32 x i8], align 1
  %last = getelementptr [32 x i8], ptr %a, i16 0, i16 31
  %lo = trunc i16 %x to i8
  %hi = trunc i16 %y to i8
  store volatile i8 %lo, ptr %a
  store volatile i8 %hi, ptr %last
  %v = load i8, ptr %p
  %w = zext i8 %v to i16
  %sum = add i16 %x, %y
  %sum2 = add i16 %sum, %z
  %r = add i16 %sum2, %w
  ret i16 %r
}

; Small frames can move SP directly and leave all input pairs intact.
; CHECK-LABEL: small:
; CHECK: dec sp
; CHECK-NOT: ld iy,
; CHECK: ret
define fastcc i16 @small(ptr %p, i16 %x, i16 %y, i16 %z) noinline optsize {
  %a = alloca i8, align 1
  %lo = trunc i16 %x to i8
  store volatile i8 %lo, ptr %a
  %v = load i8, ptr %p
  %w = zext i8 %v to i16
  %sum = add i16 %x, %y
  %sum2 = add i16 %sum, %z
  %r = add i16 %sum2, %w
  ret i16 %r
}

define i16 @caller() optsize {
  %r = call fastcc i16 @large(ptr inttoptr (i16 24576 to ptr), i16 17, i16 33, i16 32767)
  ret i16 %r
}
