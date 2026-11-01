; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc  -verify-machineinstrs %s -o %t.z80.s
; RUN: %python %S/Inputs/byte-copy-cursors.py %t.z80.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/byte-copy-cursors.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/byte-copy-cursors.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/byte-copy-cursors.py %t.greedy-bank.s

; Byte copies can retain separate source and destination cursors. Accumulator
; stores allow BC/DE destinations while HL addresses the source. Aliasing and
; volatile accesses retain their original order.
target triple = "z80"

define void @forward(ptr %source, ptr %destination) {
; CHECK-LABEL: name: forward
; CHECK: LD8pa
; CHECK: INC16r
; CHECK: LD8pa
  %v0 = load i8, ptr %source
  store i8 %v0, ptr %destination
  %s1 = getelementptr i8, ptr %source, i16 1
  %d1 = getelementptr i8, ptr %destination, i16 1
  %v1 = load i8, ptr %s1
  store i8 %v1, ptr %d1
  %s2 = getelementptr i8, ptr %source, i16 2
  %d2 = getelementptr i8, ptr %destination, i16 2
  %v2 = load i8, ptr %s2
  store i8 %v2, ptr %d2
  %s3 = getelementptr i8, ptr %source, i16 3
  %d3 = getelementptr i8, ptr %destination, i16 3
  %v3 = load i8, ptr %s3
  store i8 %v3, ptr %d3
  ret void
}

define void @backward(ptr %source, ptr %destination) {
; CHECK-LABEL: name: backward
; CHECK: LD8pa
; CHECK: DEC16r
; CHECK: LD8pa
  %v0 = load i8, ptr %source
  store i8 %v0, ptr %destination
  %s1 = getelementptr i8, ptr %source, i16 -1
  %d1 = getelementptr i8, ptr %destination, i16 -1
  %v1 = load i8, ptr %s1
  store i8 %v1, ptr %d1
  %s2 = getelementptr i8, ptr %source, i16 -2
  %d2 = getelementptr i8, ptr %destination, i16 -2
  %v2 = load i8, ptr %s2
  store i8 %v2, ptr %d2
  %s3 = getelementptr i8, ptr %source, i16 -3
  %d3 = getelementptr i8, ptr %destination, i16 -3
  %v3 = load i8, ptr %s3
  store i8 %v3, ptr %d3
  ret void
}

define void @ordered(ptr %source, ptr %destination) {
; CHECK-LABEL: name: ordered
; CHECK: LD8pa
; CHECK: INC16r
; CHECK: LD8pa
  %v0 = load volatile i8, ptr %source
  store volatile i8 %v0, ptr %destination
  %s1 = getelementptr i8, ptr %source, i16 1
  %d1 = getelementptr i8, ptr %destination, i16 1
  %v1 = load volatile i8, ptr %s1
  store volatile i8 %v1, ptr %d1
  %s2 = getelementptr i8, ptr %source, i16 2
  %d2 = getelementptr i8, ptr %destination, i16 2
  %v2 = load volatile i8, ptr %s2
  store volatile i8 %v2, ptr %d2
  %s3 = getelementptr i8, ptr %source, i16 3
  %d3 = getelementptr i8, ptr %destination, i16 3
  %v3 = load volatile i8, ptr %s3
  store volatile i8 %v3, ptr %d3
  ret void
}
