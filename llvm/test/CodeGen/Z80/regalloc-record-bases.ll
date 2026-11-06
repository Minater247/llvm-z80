; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/regalloc-record-bases.py llc %s %t
;
; Keep neighboring fields available through a clobbering call and conditional
; updates, including a field first accessed in a dominating child block.
;
; CHECK-LABEL: name: record
; CHECK: %[[BASE:[0-9]+]]:i16 = ADD16ao
; CHECK: LD8gp %[[BASE]]
; CHECK: LD8go %[[BASE]], 1
; CHECK: LD8go %[[BASE]], 5
; CHECK: CALL16 @opaque
; CHECK: LD8pa %[[BASE]]
; CHECK: LD8og %[[BASE]], 1
; CHECK: LD8go %[[BASE]], 3
; CHECK: LD8og %[[BASE]], 1
; CHECK: LD8og %[[BASE]], 3
; CHECK: LD8og %[[BASE]], 5

target triple = "z80"
declare void @opaque() nounwind

define void @record(ptr %object, i8 %mode) minsize {
entry:
  %p0 = getelementptr i8, ptr %object, i16 300
  %p1 = getelementptr i8, ptr %object, i16 301
  %p5 = getelementptr i8, ptr %object, i16 305
  %a = load volatile i8, ptr %p0
  %b = load volatile i8, ptr %p1
  %c = load volatile i8, ptr %p5
  call void @opaque()
  %test = icmp eq i8 %mode, 0
  br i1 %test, label %left, label %right
left:
  store volatile i8 %c, ptr %p0
  store volatile i8 %a, ptr %p1
  br label %exit
right:
  %p3 = getelementptr i8, ptr %object, i16 303
  %d = load volatile i8, ptr %p3
  store volatile i8 %d, ptr %p1
  br label %right.end
right.end:
  store volatile i8 %b, ptr %p3
  br label %exit
exit:
  store volatile i8 %b, ptr %p5
  ret void
}
