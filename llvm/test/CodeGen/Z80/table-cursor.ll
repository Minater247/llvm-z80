; Consecutive table indices share a cursor instead of forcing IX/IY offsets.
; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/cursor-values.py %t.s table_sum
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/cursor-values.py %t.greedy.s table_sum
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/cursor-values.py %t.bank.s table_sum
; RUN: llc -mattr=+alternate-regs -regalloc=greedy -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/cursor-values.py %t.greedy-bank.s table_sum
target triple = "z80"

define i8 @table_sum(ptr %source, i8 %page) {
; CHECK-LABEL: name: table_sum
; CHECK: LD8ap
; CHECK: INC16r
; CHECK: LD8ap
; CHECK: INC16r
; CHECK: LD8ap
  %high = zext i8 %page to i16
  %base = shl i16 %high, 8
  %a = load i8, ptr %source
  %ai = zext i8 %a to i16
  %ab = or i16 %base, %ai
  %ap = inttoptr i16 %ab to ptr
  %av = load i8, ptr %ap
  %s1 = getelementptr i8, ptr %source, i16 1
  %b = load i8, ptr %s1
  %bi = zext i8 %b to i16
  %bb = or i16 %base, %bi
  %bp = inttoptr i16 %bb to ptr
  %bv = load i8, ptr %bp
  %s2 = getelementptr i8, ptr %source, i16 2
  %c = load i8, ptr %s2
  %ci = zext i8 %c to i16
  %cb = or i16 %base, %ci
  %cp = inttoptr i16 %cb to ptr
  %cv = load i8, ptr %cp
  %sum = add i8 %av, %bv
  %result = add i8 %sum, %cv
  ret i8 %result
}
