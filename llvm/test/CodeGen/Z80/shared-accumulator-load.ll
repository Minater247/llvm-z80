; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=ez80 -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=EZ80
; RUN: llc -regalloc=z80 -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/shared-accumulator-load.py %t.s
; RUN: llc -regalloc=z80 -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/shared-accumulator-load.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/shared-accumulator-load.py %t.greedy.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/shared-accumulator-load.py %t.greedy-bank.s
;
; Several comparisons share one accumulator load. The copied value remains
; available for bit tests and conditional stores; the pointer can use BC/DE.
; CHECK-LABEL: name: classify
; CHECK: $a = LD8ap
; CHECK-NEXT: {{%[0-9]+}}:g8 = COPY $a
; EZ80-LABEL: name: classify
; EZ80: LD8gp
;
; Stores alone and a comparison using the loaded byte as its second operand
; retain the general load choice.
; CHECK-LABEL: name: two_stores
; CHECK: LD8gp
; CHECK-LABEL: name: other_compare
; CHECK: LD8gp
target triple = "z80"
define i8 @classify(ptr %p, ptr %out) {
entry:
  %x = load volatile i8, ptr %p
  %small = icmp ult i8 %x, 12
  br i1 %small, label %low, label %high
low:
  store volatile i8 %x, ptr %out
  ret i8 0
high:
  %negative = icmp slt i8 %x, 0
  br i1 %negative, label %neg, label %middle
neg:
  ret i8 3
middle:
  %bound = icmp ult i8 %x, 97
  br i1 %bound, label %yes, label %no
yes:
  ret i8 1
no:
  ret i8 2
}
define void @two_stores(ptr %p, ptr %out, ptr %out2) {
  %x = load volatile i8, ptr %p
  store volatile i8 %x, ptr %out
  store volatile i8 %x, ptr %out2
  ret void
}
define i8 @other_compare(ptr %p, i8 %other) {
  %x = load volatile i8, ptr %p
  %a = icmp ult i8 %other, %x
  br i1 %a, label %yes, label %no
yes:
  ret i8 0
no:
  %b = icmp ult i8 %x, 31
  br i1 %b, label %one, label %two
one:
  ret i8 1
two:
  ret i8 2
}
