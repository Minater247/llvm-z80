; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/saturating-add.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/saturating-add.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o - | FileCheck %s
;
; The overflow/carry flags distinguish both signs of saturation. The internal
; branch stays atomic until emission; F is a clobber, never an arithmetic result.
; CHECK-LABEL: sat:
; CHECK: add a,
; CHECK-NEXT: jp po, [[DONE:.Lsat[0-9]+]]
; CHECK-NEXT: ld a, 127
; CHECK-NEXT: adc a, 0
; CHECK: [[DONE]]:
; CHECK: ret
; CHECK-LABEL: sat_positive:
; CHECK: add a, 127
; CHECK-NEXT: jp po,
; CHECK: ret
; CHECK-LABEL: sat_negative:
; CHECK: add a, -128
; CHECK-NEXT: jp po,
; CHECK: ret
; CHECK-LABEL: sat_memory:
; CHECK: add a, ({{hl|ix|iy}})
; CHECK-NEXT: jp po,
; CHECK: ret

target triple = "z80"
declare i8 @llvm.sadd.sat.i8(i8, i8)
define i8 @sat(i8 %x, i8 %y) {
  %s = call i8 @llvm.sadd.sat.i8(i8 %x, i8 %y)
  ret i8 %s
}
define i8 @sat_positive(i8 %x) {
  %s = call i8 @llvm.sadd.sat.i8(i8 127, i8 %x)
  ret i8 %s
}
define i8 @sat_negative(i8 %x) {
  %s = call i8 @llvm.sadd.sat.i8(i8 %x, i8 -128)
  ret i8 %s
}
define i8 @sat_memory(i8 %x, ptr %p) {
  %y = load i8, ptr %p
  %s = call i8 @llvm.sadd.sat.i8(i8 %x, i8 %y)
  ret i8 %s
}
