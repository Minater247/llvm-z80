; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/cursor-call-reuse.py llc %s %t.dir
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
target triple = "z80"
declare void @opaque()
declare fastcc void @indexed(ptr)

; The first cursor already survives the call to supply its later load.
define i16 @reuse(i16 %base, i8 %choice) nounwind {
; CHECK-LABEL: name: reuse
; CHECK: [[FIRST:%[0-9]+]]:r16 = INC16r
; CHECK: CALL16 @opaque
; CHECK: bb.{{[0-9]+}}.tail:
; CHECK: INC16r [[FIRST]]
entry:
  %first = add i16 %base, 1
  call void @opaque()
  %p = inttoptr i16 %first to ptr
  %a = load volatile i8, ptr %p
  store volatile i8 %a, ptr inttoptr (i16 8960 to ptr)
  %test = icmp eq i8 %choice, 0
  br i1 %test, label %tail, label %exit
tail:
  %second = add i16 %base, 2
  ret i16 %second
exit:
  ret i16 %first
}

; A primary index argument restricts where an advancing cursor can reside.
; Preserve the independent address calculation for this call configuration.
define i16 @index_argument(i16 %base, i8 %choice) nounwind {
; CHECK-LABEL: name: index_argument
; CHECK: CALL16 @indexed{{.*}}implicit $iy
; CHECK: bb.{{[0-9]+}}.tail:
; CHECK: ADD16ao
entry:
  %first = add i16 %base, 1
  call fastcc void @indexed(ptr inttoptr (i16 9000 to ptr))
  %p = inttoptr i16 %first to ptr
  %a = load volatile i8, ptr %p
  store volatile i8 %a, ptr inttoptr (i16 8960 to ptr)
  %test = icmp eq i8 %choice, 0
  br i1 %test, label %tail, label %exit
tail:
  %second = add i16 %base, 2
  ret i16 %second
exit:
  ret i16 %first
}
