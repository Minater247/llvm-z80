; Optimized translation preserves edge weights for allocation and layout.
; RUN: llc -O2 -stop-after=irtranslator %s -o - | FileCheck %s
; RUN: llc -O0 -stop-after=irtranslator %s -o - | FileCheck %s --check-prefix=O0
target triple = "z80"

define i8 @weighted(i8 %x) {
; CHECK-LABEL: name: weighted
; CHECK: successors: %bb.{{[0-9]+}}(0x73333333), %bb.{{[0-9]+}}(0x0ccccccd)
; O0-LABEL: name: weighted
; O0: successors: %bb.{{[0-9]+}}(0x40000000), %bb.{{[0-9]+}}(0x40000000)
entry:
  %condition = icmp eq i8 %x, 0
  br i1 %condition, label %hot, label %cold, !prof !0
hot:
  ret i8 1
cold:
  ret i8 2
}

!0 = !{!"branch_weights", i32 9, i32 1}
