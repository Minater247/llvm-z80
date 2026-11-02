; RUN: llc -stop-after=z80-postlegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=ez80 -stop-after=z80-postlegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=EZ80
; RUN: %python %S/Inputs/select-constant-byte.py --llc llc --ir %s
;
; Select only the differing lane of a pair of constants, preserving both
; alternatives and the original condition. Pointers exercise the same combine
; through G_INTTOPTR. Native eZ80 word operations retain the word select.
target triple = "z80"

; CHECK-LABEL: name: low_byte
; CHECK: {{%[0-9]+}}:_(s8) = G_SELECT
; CHECK: {{%[0-9]+}}:_(s16) = G_MERGE_VALUES
; EZ80-LABEL: name: low_byte
; EZ80: {{%[0-9]+}}:_(s16) = G_SELECT
define i16 @low_byte(i8 %condition) {
  %c = icmp ult i8 %condition, 83
  %v = select i1 %c, i16 292, i16 260
  ret i16 %v
}

; CHECK-LABEL: name: high_byte
; CHECK: {{%[0-9]+}}:_(s8) = G_SELECT
; CHECK: {{%[0-9]+}}:_(s16) = G_MERGE_VALUES
define i16 @high_byte(i8 %condition) {
  %c = icmp slt i8 %condition, 0
  %v = select i1 %c, i16 -32511, i16 16897
  ret i16 %v
}

; CHECK-LABEL: name: both_bytes
; CHECK: {{%[0-9]+}}:_(s16) = G_SELECT
define i16 @both_bytes(i8 %condition) {
  %c = icmp eq i8 %condition, 0
  %v = select i1 %c, i16 292, i16 515
  ret i16 %v
}

; CHECK-LABEL: name: selected_pointer
; CHECK: {{%[0-9]+}}:_(s8) = G_SELECT
; CHECK: {{%[0-9]+}}:_(s16) = G_MERGE_VALUES
define i8 @selected_pointer(i8 %condition) {
  %c = icmp ugt i8 %condition, 172
  %p = select i1 %c, ptr inttoptr (i16 292 to ptr), ptr inttoptr (i16 260 to ptr)
  %v = load volatile i8, ptr %p
  ret i8 %v
}
