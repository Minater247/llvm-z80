; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/centered-copy-stream.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/centered-copy-stream.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/centered-copy-stream.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -stop-after=instruction-select %s -o - | FileCheck %s
;
; Adjacent streams may approach their base from either side. Source loads
; through A can use BC/DE while destination stores keep an indexed base.
; Memory operations retain their order, including overlapping ranges.
target triple = "z80"

; CHECK-LABEL: name: ascending
; CHECK-COUNT-16: LD8ap
; CHECK: RET16
define void @ascending(ptr %source, ptr %dest) {
  %s0 = getelementptr i8, ptr %source, i16 -7
  %d0 = getelementptr i8, ptr %dest, i16 -7
  %v0 = load i8, ptr %s0, align 1
  store volatile i8 %v0, ptr %d0, align 1
  %s1 = getelementptr i8, ptr %source, i16 -6
  %d1 = getelementptr i8, ptr %dest, i16 -6
  %v1 = load i8, ptr %s1, align 1
  store volatile i8 %v1, ptr %d1, align 1
  %s2 = getelementptr i8, ptr %source, i16 -5
  %d2 = getelementptr i8, ptr %dest, i16 -5
  %v2 = load i8, ptr %s2, align 1
  store volatile i8 %v2, ptr %d2, align 1
  %s3 = getelementptr i8, ptr %source, i16 -4
  %d3 = getelementptr i8, ptr %dest, i16 -4
  %v3 = load i8, ptr %s3, align 1
  store volatile i8 %v3, ptr %d3, align 1
  %s4 = getelementptr i8, ptr %source, i16 -3
  %d4 = getelementptr i8, ptr %dest, i16 -3
  %v4 = load i8, ptr %s4, align 1
  store volatile i8 %v4, ptr %d4, align 1
  %s5 = getelementptr i8, ptr %source, i16 -2
  %d5 = getelementptr i8, ptr %dest, i16 -2
  %v5 = load i8, ptr %s5, align 1
  store volatile i8 %v5, ptr %d5, align 1
  %s6 = getelementptr i8, ptr %source, i16 -1
  %d6 = getelementptr i8, ptr %dest, i16 -1
  %v6 = load i8, ptr %s6, align 1
  store volatile i8 %v6, ptr %d6, align 1
  %s7 = getelementptr i8, ptr %source, i16 0
  %d7 = getelementptr i8, ptr %dest, i16 0
  %v7 = load i8, ptr %s7, align 1
  store volatile i8 %v7, ptr %d7, align 1
  %s8 = getelementptr i8, ptr %source, i16 1
  %d8 = getelementptr i8, ptr %dest, i16 1
  %v8 = load i8, ptr %s8, align 1
  store volatile i8 %v8, ptr %d8, align 1
  %s9 = getelementptr i8, ptr %source, i16 2
  %d9 = getelementptr i8, ptr %dest, i16 2
  %v9 = load i8, ptr %s9, align 1
  store volatile i8 %v9, ptr %d9, align 1
  %s10 = getelementptr i8, ptr %source, i16 3
  %d10 = getelementptr i8, ptr %dest, i16 3
  %v10 = load i8, ptr %s10, align 1
  store volatile i8 %v10, ptr %d10, align 1
  %s11 = getelementptr i8, ptr %source, i16 4
  %d11 = getelementptr i8, ptr %dest, i16 4
  %v11 = load i8, ptr %s11, align 1
  store volatile i8 %v11, ptr %d11, align 1
  %s12 = getelementptr i8, ptr %source, i16 5
  %d12 = getelementptr i8, ptr %dest, i16 5
  %v12 = load i8, ptr %s12, align 1
  store volatile i8 %v12, ptr %d12, align 1
  %s13 = getelementptr i8, ptr %source, i16 6
  %d13 = getelementptr i8, ptr %dest, i16 6
  %v13 = load i8, ptr %s13, align 1
  store volatile i8 %v13, ptr %d13, align 1
  %s14 = getelementptr i8, ptr %source, i16 7
  %d14 = getelementptr i8, ptr %dest, i16 7
  %v14 = load i8, ptr %s14, align 1
  store volatile i8 %v14, ptr %d14, align 1
  %s15 = getelementptr i8, ptr %source, i16 8
  %d15 = getelementptr i8, ptr %dest, i16 8
  %v15 = load i8, ptr %s15, align 1
  store volatile i8 %v15, ptr %d15, align 1
  ret void
}

; CHECK-LABEL: name: descending
; CHECK-COUNT-16: LD8ap
; CHECK: RET16
define void @descending(ptr %source, ptr %dest) {
  %s0 = getelementptr i8, ptr %source, i16 8
  %d0 = getelementptr i8, ptr %dest, i16 8
  %v0 = load i8, ptr %s0, align 1
  store volatile i8 %v0, ptr %d0, align 1
  %s1 = getelementptr i8, ptr %source, i16 7
  %d1 = getelementptr i8, ptr %dest, i16 7
  %v1 = load i8, ptr %s1, align 1
  store volatile i8 %v1, ptr %d1, align 1
  %s2 = getelementptr i8, ptr %source, i16 6
  %d2 = getelementptr i8, ptr %dest, i16 6
  %v2 = load i8, ptr %s2, align 1
  store volatile i8 %v2, ptr %d2, align 1
  %s3 = getelementptr i8, ptr %source, i16 5
  %d3 = getelementptr i8, ptr %dest, i16 5
  %v3 = load i8, ptr %s3, align 1
  store volatile i8 %v3, ptr %d3, align 1
  %s4 = getelementptr i8, ptr %source, i16 4
  %d4 = getelementptr i8, ptr %dest, i16 4
  %v4 = load i8, ptr %s4, align 1
  store volatile i8 %v4, ptr %d4, align 1
  %s5 = getelementptr i8, ptr %source, i16 3
  %d5 = getelementptr i8, ptr %dest, i16 3
  %v5 = load i8, ptr %s5, align 1
  store volatile i8 %v5, ptr %d5, align 1
  %s6 = getelementptr i8, ptr %source, i16 2
  %d6 = getelementptr i8, ptr %dest, i16 2
  %v6 = load i8, ptr %s6, align 1
  store volatile i8 %v6, ptr %d6, align 1
  %s7 = getelementptr i8, ptr %source, i16 1
  %d7 = getelementptr i8, ptr %dest, i16 1
  %v7 = load i8, ptr %s7, align 1
  store volatile i8 %v7, ptr %d7, align 1
  %s8 = getelementptr i8, ptr %source, i16 0
  %d8 = getelementptr i8, ptr %dest, i16 0
  %v8 = load i8, ptr %s8, align 1
  store volatile i8 %v8, ptr %d8, align 1
  %s9 = getelementptr i8, ptr %source, i16 -1
  %d9 = getelementptr i8, ptr %dest, i16 -1
  %v9 = load i8, ptr %s9, align 1
  store volatile i8 %v9, ptr %d9, align 1
  %s10 = getelementptr i8, ptr %source, i16 -2
  %d10 = getelementptr i8, ptr %dest, i16 -2
  %v10 = load i8, ptr %s10, align 1
  store volatile i8 %v10, ptr %d10, align 1
  %s11 = getelementptr i8, ptr %source, i16 -3
  %d11 = getelementptr i8, ptr %dest, i16 -3
  %v11 = load i8, ptr %s11, align 1
  store volatile i8 %v11, ptr %d11, align 1
  %s12 = getelementptr i8, ptr %source, i16 -4
  %d12 = getelementptr i8, ptr %dest, i16 -4
  %v12 = load i8, ptr %s12, align 1
  store volatile i8 %v12, ptr %d12, align 1
  %s13 = getelementptr i8, ptr %source, i16 -5
  %d13 = getelementptr i8, ptr %dest, i16 -5
  %v13 = load i8, ptr %s13, align 1
  store volatile i8 %v13, ptr %d13, align 1
  %s14 = getelementptr i8, ptr %source, i16 -6
  %d14 = getelementptr i8, ptr %dest, i16 -6
  %v14 = load i8, ptr %s14, align 1
  store volatile i8 %v14, ptr %d14, align 1
  %s15 = getelementptr i8, ptr %source, i16 -7
  %d15 = getelementptr i8, ptr %dest, i16 -7
  %v15 = load i8, ptr %s15, align 1
  store volatile i8 %v15, ptr %d15, align 1
  ret void
}

