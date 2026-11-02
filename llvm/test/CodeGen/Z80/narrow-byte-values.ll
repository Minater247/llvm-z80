; RUN: llc -stop-after=z80-prelegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/narrow-byte-values.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/narrow-byte-values.py %t.bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; Byte observations permit narrowing cyclic word arithmetic. Observing a high
; byte anywhere in the recurrence preserves the whole dependent calculation.
target triple = "z80"

; CHECK-LABEL: name: byte_quotient
; CHECK: G_SUB {{%[0-9]+}}, {{%[0-9]+}}
; CHECK: {{%[0-9]+}}:_(s8) = G_PHI
; CHECK-NOT: :_(s16) = G_PHI
; CHECK: RET16
define i8 @byte_quotient(i16 %a, i16 %b) {
entry:
  %difference = sub i16 %a, %b
  br label %loop
loop:
  %wide = phi i16 [ %difference, %entry ], [ %next, %body ]
  %count = phi i8 [ 0, %entry ], [ %inc, %body ]
  %value = and i16 %wide, 255
  %more = icmp ugt i16 %value, 16
  br i1 %more, label %body, label %exit
body:
  %next = add nsw i16 %value, -17
  %inc = add i8 %count, 1
  br label %loop
exit:
  ret i8 %count
}

; CHECK-LABEL: name: coupled_bytes
; CHECK-NOT: :_(s16) = G_PHI
; CHECK: RET16
define i8 @coupled_bytes(i16 %a, i16 %b) {
entry:
  br label %loop
loop:
  %x = phi i16 [ %a, %entry ], [ %nextx, %loop ]
  %y = phi i16 [ %b, %entry ], [ %nexty, %loop ]
  %count = phi i8 [ 7, %entry ], [ %dec, %loop ]
  %sum = add i16 %x, %y
  %nextx = xor i16 %sum, 42293
  %nexty = sub i16 %x, %y
  %dec = add i8 %count, -1
  %more = icmp ne i8 %dec, 0
  br i1 %more, label %loop, label %exit
exit:
  %mix = or i16 %nextx, %nexty
  %result = trunc i16 %mix to i8
  ret i8 %result
}

; CHECK-LABEL: name: high_observation
; CHECK: {{%[0-9]+}}:_(s16) = G_PHI
; CHECK: RET16
define i8 @high_observation(i16 %a, i16 %b) {
entry:
  br label %loop
loop:
  %x = phi i16 [ %a, %entry ], [ %nextx, %loop ]
  %y = phi i16 [ %b, %entry ], [ %nexty, %loop ]
  %count = phi i8 [ 7, %entry ], [ %dec, %loop ]
  %sum = add i16 %x, %y
  %nextx = xor i16 %sum, 42293
  %nexty = sub i16 %x, %y
  %dec = add i8 %count, -1
  %more = icmp ne i8 %dec, 0
  br i1 %more, label %loop, label %exit
exit:
  %mix = or i16 %nextx, %nexty
  %high = lshr i16 %mix, 8
  %result = trunc i16 %high to i8
  ret i8 %result
}

; CHECK-LABEL: name: full_result
; CHECK: {{%[0-9]+}}:_(s16) = G_SUB
; CHECK: RET16
define i16 @full_result(i16 %a, i16 %b) {
  %diff = sub i16 %a, %b
  %low = and i16 %diff, 255
  %result = xor i16 %diff, %low
  ret i16 %result
}
