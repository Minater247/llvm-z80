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

; Integer addresses retain the same unit-update choices as pointer GEPs.
define void @integer_forward(i16 %source, i16 %destination) {
; CHECK-LABEL: name: integer_forward
; CHECK: %[[IFS1:[0-9]+]]:r16 = INC16r
; CHECK: %[[IFD1:[0-9]+]]:r16 = INC16r
; CHECK: %[[IFS2:[0-9]+]]:r16 = INC16r %[[IFS1]]
; CHECK: %[[IFD2:[0-9]+]]:r16 = INC16r %[[IFD1]]
; CHECK: %[[IFS3:[0-9]+]]:r16 = INC16r %[[IFS2]]
; CHECK: %[[IFD3:[0-9]+]]:r16 = INC16r %[[IFD2]]
  %sp0 = inttoptr i16 %source to ptr
  %dp0 = inttoptr i16 %destination to ptr
  %v0 = load i8, ptr %sp0
  store i8 %v0, ptr %dp0
  %s1 = add i16 %source, 1
  %d1 = add i16 %destination, 1
  %sp1 = inttoptr i16 %s1 to ptr
  %dp1 = inttoptr i16 %d1 to ptr
  %v1 = load i8, ptr %sp1
  store i8 %v1, ptr %dp1
  %s2 = add i16 %source, 2
  %d2 = add i16 %destination, 2
  %sp2 = inttoptr i16 %s2 to ptr
  %dp2 = inttoptr i16 %d2 to ptr
  %v2 = load i8, ptr %sp2
  store i8 %v2, ptr %dp2
  %s3 = add i16 %source, 3
  %d3 = add i16 %destination, 3
  %sp3 = inttoptr i16 %s3 to ptr
  %dp3 = inttoptr i16 %d3 to ptr
  %v3 = load i8, ptr %sp3
  store i8 %v3, ptr %dp3
  ret void
}

; Integer addresses retain the same unit-update choices as pointer GEPs.
define void @integer_backward(i16 %source, i16 %destination) {
; CHECK-LABEL: name: integer_backward
; CHECK: %[[IBS1:[0-9]+]]:r16 = DEC16r
; CHECK: %[[IBD1:[0-9]+]]:r16 = DEC16r
; CHECK: %[[IBS2:[0-9]+]]:r16 = DEC16r %[[IBS1]]
; CHECK: %[[IBD2:[0-9]+]]:r16 = DEC16r %[[IBD1]]
; CHECK: %[[IBS3:[0-9]+]]:r16 = DEC16r %[[IBS2]]
; CHECK: %[[IBD3:[0-9]+]]:r16 = DEC16r %[[IBD2]]
  %sp0 = inttoptr i16 %source to ptr
  %dp0 = inttoptr i16 %destination to ptr
  %v0 = load i8, ptr %sp0
  store i8 %v0, ptr %dp0
  %s1 = add i16 %source, -1
  %d1 = add i16 %destination, -1
  %sp1 = inttoptr i16 %s1 to ptr
  %dp1 = inttoptr i16 %d1 to ptr
  %v1 = load i8, ptr %sp1
  store i8 %v1, ptr %dp1
  %s2 = add i16 %source, -2
  %d2 = add i16 %destination, -2
  %sp2 = inttoptr i16 %s2 to ptr
  %dp2 = inttoptr i16 %d2 to ptr
  %v2 = load i8, ptr %sp2
  store i8 %v2, ptr %dp2
  %s3 = add i16 %source, -3
  %d3 = add i16 %destination, -3
  %sp3 = inttoptr i16 %s3 to ptr
  %dp3 = inttoptr i16 %d3 to ptr
  %v3 = load i8, ptr %sp3
  store i8 %v3, ptr %dp3
  ret void
}

; Integer addresses retain the same unit-update choices as pointer GEPs.
define void @integer_ordered(i16 %source, i16 %destination) {
; CHECK-LABEL: name: integer_ordered
; CHECK: %[[IOS1:[0-9]+]]:r16 = INC16r
; CHECK: %[[IOD1:[0-9]+]]:r16 = INC16r
; CHECK: %[[IOS2:[0-9]+]]:r16 = INC16r %[[IOS1]]
; CHECK: %[[IOD2:[0-9]+]]:r16 = INC16r %[[IOD1]]
; CHECK: %[[IOS3:[0-9]+]]:r16 = INC16r %[[IOS2]]
; CHECK: %[[IOD3:[0-9]+]]:r16 = INC16r %[[IOD2]]
  %sp0 = inttoptr i16 %source to ptr
  %dp0 = inttoptr i16 %destination to ptr
  %v0 = load volatile i8, ptr %sp0
  store volatile i8 %v0, ptr %dp0
  %s1 = add i16 %source, 1
  %d1 = add i16 %destination, 1
  %sp1 = inttoptr i16 %s1 to ptr
  %dp1 = inttoptr i16 %d1 to ptr
  %v1 = load volatile i8, ptr %sp1
  store volatile i8 %v1, ptr %dp1
  %s2 = add i16 %source, 2
  %d2 = add i16 %destination, 2
  %sp2 = inttoptr i16 %s2 to ptr
  %dp2 = inttoptr i16 %d2 to ptr
  %v2 = load volatile i8, ptr %sp2
  store volatile i8 %v2, ptr %dp2
  %s3 = add i16 %source, 3
  %d3 = add i16 %destination, 3
  %sp3 = inttoptr i16 %s3 to ptr
  %dp3 = inttoptr i16 %d3 to ptr
  %v3 = load volatile i8, ptr %sp3
  store volatile i8 %v3, ptr %dp3
  ret void
}

; Large absolute displacements still differ by one between adjacent fields.
define void @large_forward(ptr %source, ptr %destination) {
; CHECK-LABEL: name: large_forward
; CHECK: %[[LS1:[0-9]+]]:{{[a-z0-9]+}} = INC16r
; CHECK: %[[LD1:[0-9]+]]:{{[a-z0-9]+}} = INC16r
; CHECK: %[[LS2:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LS1]]
; CHECK: %[[LD2:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LD1]]
; CHECK: %[[LS3:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LS2]]
; CHECK: %[[LD3:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LD2]]
  %sp0 = getelementptr i8, ptr %source, i16 4096
  %dp0 = getelementptr i8, ptr %destination, i16 4096
  %v0 = load i8, ptr %sp0
  store i8 %v0, ptr %dp0
  %sp1 = getelementptr i8, ptr %source, i16 4097
  %dp1 = getelementptr i8, ptr %destination, i16 4097
  %v1 = load i8, ptr %sp1
  store i8 %v1, ptr %dp1
  %sp2 = getelementptr i8, ptr %source, i16 4098
  %dp2 = getelementptr i8, ptr %destination, i16 4098
  %v2 = load i8, ptr %sp2
  store i8 %v2, ptr %dp2
  %sp3 = getelementptr i8, ptr %source, i16 4099
  %dp3 = getelementptr i8, ptr %destination, i16 4099
  %v3 = load i8, ptr %sp3
  store i8 %v3, ptr %dp3
  ret void
}

; Large absolute displacements still differ by one between adjacent fields.
define void @large_backward(ptr %source, ptr %destination) {
; CHECK-LABEL: name: large_backward
; CHECK: %[[LS1:[0-9]+]]:{{[a-z0-9]+}} = DEC16r
; CHECK: %[[LD1:[0-9]+]]:{{[a-z0-9]+}} = DEC16r
; CHECK: %[[LS2:[0-9]+]]:{{[a-z0-9]+}} = DEC16r %[[LS1]]
; CHECK: %[[LD2:[0-9]+]]:{{[a-z0-9]+}} = DEC16r %[[LD1]]
; CHECK: %[[LS3:[0-9]+]]:{{[a-z0-9]+}} = DEC16r %[[LS2]]
; CHECK: %[[LD3:[0-9]+]]:{{[a-z0-9]+}} = DEC16r %[[LD2]]
  %sp0 = getelementptr i8, ptr %source, i16 -4096
  %dp0 = getelementptr i8, ptr %destination, i16 -4096
  %v0 = load i8, ptr %sp0
  store i8 %v0, ptr %dp0
  %sp1 = getelementptr i8, ptr %source, i16 -4097
  %dp1 = getelementptr i8, ptr %destination, i16 -4097
  %v1 = load i8, ptr %sp1
  store i8 %v1, ptr %dp1
  %sp2 = getelementptr i8, ptr %source, i16 -4098
  %dp2 = getelementptr i8, ptr %destination, i16 -4098
  %v2 = load i8, ptr %sp2
  store i8 %v2, ptr %dp2
  %sp3 = getelementptr i8, ptr %source, i16 -4099
  %dp3 = getelementptr i8, ptr %destination, i16 -4099
  %v3 = load i8, ptr %sp3
  store i8 %v3, ptr %dp3
  ret void
}

; Large absolute displacements still differ by one between adjacent fields.
define void @large_ordered(ptr %source, ptr %destination) {
; CHECK-LABEL: name: large_ordered
; CHECK: %[[LS1:[0-9]+]]:{{[a-z0-9]+}} = INC16r
; CHECK: %[[LD1:[0-9]+]]:{{[a-z0-9]+}} = INC16r
; CHECK: %[[LS2:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LS1]]
; CHECK: %[[LD2:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LD1]]
; CHECK: %[[LS3:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LS2]]
; CHECK: %[[LD3:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LD2]]
  %sp0 = getelementptr i8, ptr %source, i16 4096
  %dp0 = getelementptr i8, ptr %destination, i16 4096
  %v0 = load volatile i8, ptr %sp0
  store volatile i8 %v0, ptr %dp0
  %sp1 = getelementptr i8, ptr %source, i16 4097
  %dp1 = getelementptr i8, ptr %destination, i16 4097
  %v1 = load volatile i8, ptr %sp1
  store volatile i8 %v1, ptr %dp1
  %sp2 = getelementptr i8, ptr %source, i16 4098
  %dp2 = getelementptr i8, ptr %destination, i16 4098
  %v2 = load volatile i8, ptr %sp2
  store volatile i8 %v2, ptr %dp2
  %sp3 = getelementptr i8, ptr %source, i16 4099
  %dp3 = getelementptr i8, ptr %destination, i16 4099
  %v3 = load volatile i8, ptr %sp3
  store volatile i8 %v3, ptr %dp3
  ret void
}

; Large absolute displacements still differ by one between adjacent fields.
define void @large_integer(i16 %source, i16 %destination) {
; CHECK-LABEL: name: large_integer
; CHECK: %[[LS1:[0-9]+]]:{{[a-z0-9]+}} = INC16r
; CHECK: %[[LD1:[0-9]+]]:{{[a-z0-9]+}} = INC16r
; CHECK: %[[LS2:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LS1]]
; CHECK: %[[LD2:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LD1]]
; CHECK: %[[LS3:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LS2]]
; CHECK: %[[LD3:[0-9]+]]:{{[a-z0-9]+}} = INC16r %[[LD2]]
  %s0 = add i16 %source, 4096
  %sp0 = inttoptr i16 %s0 to ptr
  %d0 = add i16 %destination, 4096
  %dp0 = inttoptr i16 %d0 to ptr
  %v0 = load i8, ptr %sp0
  store i8 %v0, ptr %dp0
  %s1 = add i16 %source, 4097
  %sp1 = inttoptr i16 %s1 to ptr
  %d1 = add i16 %destination, 4097
  %dp1 = inttoptr i16 %d1 to ptr
  %v1 = load i8, ptr %sp1
  store i8 %v1, ptr %dp1
  %s2 = add i16 %source, 4098
  %sp2 = inttoptr i16 %s2 to ptr
  %d2 = add i16 %destination, 4098
  %dp2 = inttoptr i16 %d2 to ptr
  %v2 = load i8, ptr %sp2
  store i8 %v2, ptr %dp2
  %s3 = add i16 %source, 4099
  %sp3 = inttoptr i16 %s3 to ptr
  %d3 = add i16 %destination, 4099
  %dp3 = inttoptr i16 %d3 to ptr
  %v3 = load i8, ptr %sp3
  store i8 %v3, ptr %dp3
  ret void
}
