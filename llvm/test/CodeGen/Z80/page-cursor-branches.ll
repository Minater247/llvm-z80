; RUN: llc -verify-machineinstrs -stop-after=instruction-select %s -o - | FileCheck %s
; RUN: %python %S/Inputs/page-cursor-branches.py llc %s %t.dir
; A page-preserving cursor remains a pair through a dominating branch.
target triple = "z80"

define i16 @advance(i16 %start, i8 %choice) nounwind {
; CHECK-LABEL: name: advance
; CHECK-COUNT-2: INCL16
  %page = and i16 %start, -256
  %p = inttoptr i16 %start to ptr
  store volatile i8 7, ptr %p
  %step1 = add i16 %start, 1
  %low1 = and i16 %step1, 255
  %next1 = or i16 %page, %low1
  %p1 = inttoptr i16 %next1 to ptr
  store volatile i8 9, ptr %p1
  %bit = and i8 %choice, 1
  %test = icmp eq i8 %bit, 0
  br i1 %test, label %left, label %right
left:
  store volatile i8 11, ptr %p
  br label %join
right:
  store volatile i8 13, ptr %p1
  br label %join
join:
  %step2 = add i16 %start, 2
  %low2 = and i16 %step2, 255
  %next2 = or i16 %page, %low2
  %p2 = inttoptr i16 %next2 to ptr
  store volatile i8 17, ptr %p2
  ret i16 %next2
}
