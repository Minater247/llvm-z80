; RUN: llc -mtriple=z80 -verify-machineinstrs -stop-after=prologepilog %s -o - | FileCheck %s
; RUN: llc -mtriple=z80 -regalloc=greedy -verify-machineinstrs %s -o /dev/null
;
; Size-optimized frame setup must provide the ADD's tied source and an LD SP
; source use (not a register definition), with and without local stack slots.
; CHECK-LABEL: name: without_locals
; CHECK: $ix = frame-setup ADD16as $ix
; CHECK-LABEL: name: with_locals
; CHECK: $ix = frame-setup ADD16as $ix
; CHECK: $iy = frame-setup ADD16as $iy
; CHECK: frame-setup LD16sa killed $iy

declare void @consume(ptr)

define i8 @without_locals(ptr %p) optsize {
  %v = load volatile i8, ptr %p
  ret i8 %v
}

define void @with_locals() optsize {
  %buffer = alloca [4 x i8], align 1
  call void @consume(ptr %buffer)
  ret void
}
