; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o - | FileCheck %s

target triple = "z80"

declare void @callee()
@input = external global i8

; A register-only leaf preserves IX without saving or establishing it.
; CHECK-LABEL: leaf:
; CHECK-NOT: ix
; CHECK: ret
; CHECK-NOT: ix
define i8 @leaf() nounwind minsize {
  %x = load volatile i8, ptr @input
  %r = add i8 %x, 3
  ret i8 %r
}

; A tail jump needs no frame when it has no stack arguments or saved values.
; CHECK-LABEL: tail:
; CHECK-NOT: ix
; CHECK: jp{{[ \t]+}}callee
; CHECK-NOT: ix
define void @tail() nounwind minsize {
  tail call void @callee()
  ret void
}

; Explicit frame-pointer requests still establish and restore the frame.
; CHECK-LABEL: requested:
; CHECK: {{push.*ix|call.*_frameset0}}
; CHECK: pop{{[ \t]+}}ix
; CHECK: ret
define i8 @requested(i8 %x) nounwind minsize "frame-pointer"="all" {
  ret i8 %x
}

; A live frame object retains its base, including for byte accesses.
; CHECK-LABEL: storage:
; CHECK: {{push.*ix|call.*_frameset}}
; CHECK: {{ld.*\(ix|lea}}
; CHECK: ret
define i8 @storage(i8 %x) nounwind minsize {
  %p = alloca i8
  store volatile i8 %x, ptr %p
  %r = load volatile i8, ptr %p
  ret i8 %r
}

; A register-argument call can preserve its return address on SP directly.
; CHECK-LABEL: ordinary_call:
; CHECK-NOT: ix
; CHECK: call{{[ \t]+}}callee
; CHECK-NOT: ix
; CHECK: ret
define i8 @ordinary_call() nounwind minsize {
  call void @callee()
  ret i8 7
}

; Unwind metadata continues to use the existing frame representation.
; CHECK-LABEL: unwind:
; CHECK: {{push.*ix|call.*_frameset0}}
; CHECK: .cfi_
; CHECK: ret
define i8 @unwind() nounwind minsize uwtable {
  ret i8 7
}
