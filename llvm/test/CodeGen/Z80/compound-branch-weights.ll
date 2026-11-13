; RUN: llc -stop-before=irtranslator -verify-machineinstrs %s -o - | FileCheck %s

target triple = "z80"

; Splitting an OR preserves its total true probability:
; 10/200 + (190/200 * 10/190) = 10/100.
; CHECK-LABEL: define i8 @either(
; CHECK: br i1 %first, label %yes, label %entry.cond.split, !prof ![[OR_FIRST:[0-9]+]]
; CHECK: br i1 %second, label %yes, label %no, !prof ![[OR_SECOND:[0-9]+]]
define i8 @either(i8 %a, i8 %b) {
entry:
  %first = icmp ult i8 %a, 37
  %second = icmp eq i8 %b, 193
  %test = or i1 %first, %second
  br i1 %test, label %yes, label %no, !prof !0
yes:
  ret i8 %a
no:
  ret i8 %b
}

; Splitting an AND preserves its total true probability:
; 110/200 * 20/110 = 10/100.
; CHECK-LABEL: define i8 @both(
; CHECK: br i1 %first, label %entry.cond.split, label %no, !prof ![[AND_FIRST:[0-9]+]]
; CHECK: br i1 %second, label %yes, label %no, !prof ![[AND_SECOND:[0-9]+]]
define i8 @both(i8 %a, i8 %b) {
entry:
  %first = icmp ult i8 %a, 37
  %second = icmp eq i8 %b, 193
  %test = and i1 %first, %second
  br i1 %test, label %yes, label %no, !prof !0
yes:
  ret i8 %a
no:
  ret i8 %b
}

; The split weights can exceed uint32_t even though the input weights fit.
; Check the scaled values, printed as signed i32, rather than truncated sums.
; CHECK-LABEL: define i8 @either_large(
; CHECK: br i1 %first, label %yes, label %entry.cond.split, !prof ![[LARGE_OR_FIRST:[0-9]+]]
; CHECK: br i1 %second, label %yes, label %no, !prof ![[LARGE_OR_SECOND:[0-9]+]]
define i8 @either_large(i8 %a, i8 %b) {
entry:
  %first = icmp ult i8 %a, 37
  %second = icmp eq i8 %b, 193
  %test = or i1 %first, %second
  br i1 %test, label %yes, label %no, !prof !1
yes:
  ret i8 %a
no:
  ret i8 %b
}

; CHECK-LABEL: define i8 @both_large(
; CHECK: br i1 %first, label %entry.cond.split, label %no, !prof ![[LARGE_AND_FIRST:[0-9]+]]
; CHECK: br i1 %second, label %yes, label %no, !prof ![[LARGE_AND_SECOND:[0-9]+]]
define i8 @both_large(i8 %a, i8 %b) {
entry:
  %first = icmp ult i8 %a, 37
  %second = icmp eq i8 %b, 193
  %test = and i1 %first, %second
  br i1 %test, label %yes, label %no, !prof !1
yes:
  ret i8 %a
no:
  ret i8 %b
}

; CHECK-DAG: ![[OR_FIRST]] = !{!"branch_weights", i32 10, i32 190}
; CHECK-DAG: ![[OR_SECOND]] = !{!"branch_weights", i32 10, i32 180}
; CHECK-DAG: ![[AND_FIRST]] = !{!"branch_weights", i32 110, i32 90}
; CHECK-DAG: ![[AND_SECOND]] = !{!"branch_weights", i32 20, i32 90}
; CHECK-DAG: ![[LARGE_OR_FIRST]] = !{!"branch_weights", i32 1073741823, i32 -1073741825}
; CHECK-DAG: ![[LARGE_OR_SECOND]] = !{!"branch_weights", i32 1431655765, i32 -1431655766}
; CHECK-DAG: ![[LARGE_AND_FIRST]] = !{!"branch_weights", i32 -1073741825, i32 1073741823}
; CHECK-DAG: ![[LARGE_AND_SECOND]] = !{!"branch_weights", i32 -1431655766, i32 1431655765}

!0 = !{!"branch_weights", i32 10, i32 90}
!1 = !{!"branch_weights", i32 -1, i32 -1}
