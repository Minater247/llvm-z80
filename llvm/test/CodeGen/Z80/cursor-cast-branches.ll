; RUN: llc -verify-machineinstrs -stop-after=instruction-select %s -o - | FileCheck %s
; RUN: %python %S/Inputs/cursor-cast-branches.py llc %s %t.dir
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
;
; Integer addresses and equal-width pointer casts share the same cursor.
; A single predecessor supplies an earlier step without moving either store.
target triple = "z80"

define i16 @advance(i16 %base, i8 %choice) nounwind {
; CHECK-LABEL: name: advance
; CHECK: [[FIRST:%[0-9]+]]:r16 = INC16r
; CHECK: bb.{{[0-9]+}}.tail:
; CHECK: [[SECOND:%[0-9]+]]:a16 = INC16r [[FIRST]]
; CHECK: INC16r [[SECOND]]
entry:
  %first = add i16 %base, 1
  %p1 = inttoptr i16 %first to ptr
  store volatile i8 17, ptr %p1
  %test = icmp eq i8 %choice, 0
  br i1 %test, label %tail, label %exit
tail:
  %pointer = inttoptr i16 %base to ptr
  %p2 = getelementptr i8, ptr %pointer, i16 2
  store volatile i8 34, ptr %p2
  %third = add i16 %base, 3
  ret i16 %third
exit:
  ret i16 %first
}

; A fixed page keeps only its changing low byte across a branch, including
; functions containing calls outside the cursor's local region.
declare void @opaque()
define i16 @page_advance(i16 %base, i8 %choice) nounwind {
; CHECK-LABEL: name: page_advance
; CHECK: bb.{{[0-9]+}}.tail:
; CHECK: COPY {{%[0-9]+}}.sub_low
; CHECK: INC8r
; CHECK: INSERT_SUBREG
entry:
  call void @opaque()
  %first = add i16 %base, 1
  %lo1 = and i16 %first, 255
  %address1 = or i16 %lo1, 12544
  %p1 = inttoptr i16 %address1 to ptr
  store volatile i8 17, ptr %p1
  %test = icmp eq i8 %choice, 0
  br i1 %test, label %tail, label %exit
tail:
  %second = add i16 %base, 2
  %lo2 = and i16 %second, 255
  %address2 = or i16 %lo2, 12544
  %p2 = inttoptr i16 %address2 to ptr
  store volatile i8 34, ptr %p2
  %third = add i16 %base, 3
  %lo3 = and i16 %third, 255
  %address3 = or i16 %lo3, 12544
  ret i16 %address3
exit:
  ret i16 %address1
}

; Two intervening page addresses already compete for general pairs. Keep
; the later source computation independent instead of extending its cursor.
define i16 @pressure(i16 %base, i8 %choice) nounwind {
; CHECK-LABEL: name: pressure
; CHECK: bb.{{[0-9]+}}.tail:
; CHECK: ADD16ao
entry:
  %first = add i16 %base, 1
  %p1 = inttoptr i16 %first to ptr
  %index = load volatile i8, ptr %p1
  %wide = zext i8 %index to i16
  %a = or i16 %wide, 4096
  %ap = inttoptr i16 %a to ptr
  %x = load volatile i8, ptr %ap
  %b = or i16 %wide, 8192
  %bp = inttoptr i16 %b to ptr
  %y = load volatile i8, ptr %bp
  %test = icmp eq i8 %choice, 0
  br i1 %test, label %tail, label %exit
tail:
  %second = add i16 %base, 2
  ret i16 %second
exit:
  ret i16 %first
}

; Preserve the original rematerialization choice across an opaque call.
define i16 @call_boundary(i16 %base, i8 %choice) nounwind {
; CHECK-LABEL: name: call_boundary
; CHECK: bb.{{[0-9]+}}.tail:
; CHECK: CALL16 @opaque
; CHECK: ADD16ao
entry:
  %first = add i16 %base, 1
  %p1 = inttoptr i16 %first to ptr
  store volatile i8 17, ptr %p1
  %test = icmp eq i8 %choice, 0
  br i1 %test, label %tail, label %exit
tail:
  call void @opaque()
  %second = add i16 %base, 2
  ret i16 %second
exit:
  ret i16 %first
}

; A cursor defined on only one incoming path cannot supply the join.
define i16 @join(i16 %base, i8 %choice) nounwind {
; CHECK-LABEL: name: join
; CHECK: bb.{{[0-9]+}}.join:
; CHECK: ADD16ao
entry:
  %test = icmp eq i8 %choice, 0
  br i1 %test, label %left, label %join
left:
  %first = add i16 %base, 1
  %p1 = inttoptr i16 %first to ptr
  store volatile i8 17, ptr %p1
  br label %join
join:
  %second = add i16 %base, 2
  ret i16 %second
}
