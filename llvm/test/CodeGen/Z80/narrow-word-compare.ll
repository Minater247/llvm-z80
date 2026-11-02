; RUN: llc -stop-after=z80-postlegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/narrow-word-compare.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/narrow-word-compare.py %t.bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; Both words contain unsigned bytes. Signed word order becomes unsigned byte
; order, including values whose low-byte sign bit is set.
target triple = "z80"
; CHECK-LABEL: name: masked_unsigned
; CHECK: G_ICMP intpred(uge), {{%[0-9]+}}(s8), {{%[0-9]+}}
define i8 @masked_unsigned(i16 %a, i16 %b) {
  %x = and i16 %a, 255
  %y = and i16 %b, 255
  %test = icmp ult i16 %x, %y
  br i1 %test, label %yes, label %no
yes:
  ret i8 31
no:
  ret i8 79
}
; CHECK-LABEL: name: masked_signed
; CHECK: G_ICMP intpred(uge), {{%[0-9]+}}(s8), {{%[0-9]+}}
define i8 @masked_signed(i16 %a, i16 %b) {
  %x = and i16 %a, 255
  %y = and i16 %b, 255
  %test = icmp slt i16 %x, %y
  br i1 %test, label %yes, label %no
yes:
  ret i8 31
no:
  ret i8 79
}
; CHECK-LABEL: name: masked_equal
; CHECK: G_ICMP intpred(ne), {{%[0-9]+}}(s8), {{%[0-9]+}}
define i8 @masked_equal(i16 %a, i16 %b) {
  %x = and i16 %a, 252
  %y = and i16 %b, 255
  %test = icmp eq i16 %x, %y
  br i1 %test, label %yes, label %no
yes:
  ret i8 31
no:
  ret i8 79
}
; One unknown upper byte retains the full comparison.
; CHECK-LABEL: name: unknown_high
; CHECK: G_ICMP intpred(uge), {{%[0-9]+}}(s16), {{%[0-9]+}}
define i8 @unknown_high(i16 %a, i16 %b) {
  %y = and i16 %b, 255
  %test = icmp ult i16 %a, %y
  br i1 %test, label %yes, label %no
yes:
  ret i8 31
no:
  ret i8 79
}
