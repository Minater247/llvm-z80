; RUN: llc -mattr=+alternate-regs -verify-machineinstrs -print-regusage %s -o %t 2>&1 | FileCheck %s --check-prefix=MASK
; RUN: FileCheck %s --check-prefix=BANK < %t
; RUN: llc -mattr=+alternate-regs -enable-ipra=false -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=STACK
; RUN: llc -mattr=-alternate-regs -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=STACK
; RUN: %python %S/Inputs/alternate-calls.py llc %s %t.dir
;
; This leaf needs no late bank-region scratch. Its primary pairs, IX and the
; alternate bank remain preserved through the nested calls.
; MASK: leaf Clobbered Registers: $a $af $f $iy $iyh $iyl $sps $uiy
; BANK-LABEL: known:
; BANK: exx
; BANK: call middle
; BANK: exx
; BANK-LABEL: unknown:
; BANK-NOT: exx
; BANK: call external
; BANK-NOT: exx
; BANK: ret
; BANK-LABEL: transitive:
; BANK-NOT: exx
; BANK: call middle_unknown
; BANK-NOT: exx
; BANK: ret
; BANK-LABEL: interposable:
; BANK-NOT: exx
; BANK: call replaceable
; BANK-NOT: exx
; BANK: ret
; BANK-LABEL: recursive:
; BANK-NOT: exx
; BANK: call recur
; BANK-NOT: exx
; BANK: ret
; STACK-NOT: exx
;
target triple = "z80"
; Fixed addresses keep the execution check independent of object linking.
define internal void @leaf() noinline {
  %v = load volatile i8, ptr inttoptr (i16 8448 to ptr)
  %n = add i8 %v, 7
  store volatile i8 %n, ptr inttoptr (i16 8448 to ptr)
  ret void
}
define internal void @middle() noinline {
  call void @leaf()
  call void @leaf()
  ret void
}
declare void @external()
define internal void @middle_unknown() noinline {
  call void @external()
  ret void
}
define weak void @replaceable() noinline {
  call void @leaf()
  call void @leaf()
  ret void
}
define internal void @recur() noinline {
  %v = load volatile i8, ptr inttoptr (i16 8449 to ptr)
  %done = icmp eq i8 %v, 0
  br i1 %done, label %exit, label %body
body:
  %n = sub i8 %v, 1
  store volatile i8 %n, ptr inttoptr (i16 8449 to ptr)
  call void @recur()
  br label %exit
exit:
  ret void
}
define void @known() {
  %v0 = load volatile i16, ptr inttoptr (i16 8192 to ptr)
  %v1 = load volatile i16, ptr inttoptr (i16 8194 to ptr)
  %v2 = load volatile i16, ptr inttoptr (i16 8196 to ptr)
  %v3 = load volatile i16, ptr inttoptr (i16 8198 to ptr)
  %v4 = load volatile i16, ptr inttoptr (i16 8200 to ptr)
  %v5 = load volatile i16, ptr inttoptr (i16 8202 to ptr)
  call void @middle()
  store volatile i16 %v0, ptr inttoptr (i16 8224 to ptr)
  store volatile i16 %v1, ptr inttoptr (i16 8226 to ptr)
  store volatile i16 %v2, ptr inttoptr (i16 8228 to ptr)
  store volatile i16 %v3, ptr inttoptr (i16 8230 to ptr)
  store volatile i16 %v4, ptr inttoptr (i16 8232 to ptr)
  store volatile i16 %v5, ptr inttoptr (i16 8234 to ptr)
  ret void
}
define void @unknown() {
  %v0 = load volatile i16, ptr inttoptr (i16 8192 to ptr)
  %v1 = load volatile i16, ptr inttoptr (i16 8194 to ptr)
  %v2 = load volatile i16, ptr inttoptr (i16 8196 to ptr)
  %v3 = load volatile i16, ptr inttoptr (i16 8198 to ptr)
  %v4 = load volatile i16, ptr inttoptr (i16 8200 to ptr)
  %v5 = load volatile i16, ptr inttoptr (i16 8202 to ptr)
  call void @external()
  store volatile i16 %v0, ptr inttoptr (i16 8224 to ptr)
  store volatile i16 %v1, ptr inttoptr (i16 8226 to ptr)
  store volatile i16 %v2, ptr inttoptr (i16 8228 to ptr)
  store volatile i16 %v3, ptr inttoptr (i16 8230 to ptr)
  store volatile i16 %v4, ptr inttoptr (i16 8232 to ptr)
  store volatile i16 %v5, ptr inttoptr (i16 8234 to ptr)
  ret void
}
define void @transitive() {
  %v0 = load volatile i16, ptr inttoptr (i16 8192 to ptr)
  %v1 = load volatile i16, ptr inttoptr (i16 8194 to ptr)
  %v2 = load volatile i16, ptr inttoptr (i16 8196 to ptr)
  %v3 = load volatile i16, ptr inttoptr (i16 8198 to ptr)
  %v4 = load volatile i16, ptr inttoptr (i16 8200 to ptr)
  %v5 = load volatile i16, ptr inttoptr (i16 8202 to ptr)
  call void @middle_unknown()
  store volatile i16 %v0, ptr inttoptr (i16 8224 to ptr)
  store volatile i16 %v1, ptr inttoptr (i16 8226 to ptr)
  store volatile i16 %v2, ptr inttoptr (i16 8228 to ptr)
  store volatile i16 %v3, ptr inttoptr (i16 8230 to ptr)
  store volatile i16 %v4, ptr inttoptr (i16 8232 to ptr)
  store volatile i16 %v5, ptr inttoptr (i16 8234 to ptr)
  ret void
}
define void @interposable() {
  %v0 = load volatile i16, ptr inttoptr (i16 8192 to ptr)
  %v1 = load volatile i16, ptr inttoptr (i16 8194 to ptr)
  %v2 = load volatile i16, ptr inttoptr (i16 8196 to ptr)
  %v3 = load volatile i16, ptr inttoptr (i16 8198 to ptr)
  %v4 = load volatile i16, ptr inttoptr (i16 8200 to ptr)
  %v5 = load volatile i16, ptr inttoptr (i16 8202 to ptr)
  call void @replaceable()
  store volatile i16 %v0, ptr inttoptr (i16 8224 to ptr)
  store volatile i16 %v1, ptr inttoptr (i16 8226 to ptr)
  store volatile i16 %v2, ptr inttoptr (i16 8228 to ptr)
  store volatile i16 %v3, ptr inttoptr (i16 8230 to ptr)
  store volatile i16 %v4, ptr inttoptr (i16 8232 to ptr)
  store volatile i16 %v5, ptr inttoptr (i16 8234 to ptr)
  ret void
}
define void @recursive() {
  %v0 = load volatile i16, ptr inttoptr (i16 8192 to ptr)
  %v1 = load volatile i16, ptr inttoptr (i16 8194 to ptr)
  %v2 = load volatile i16, ptr inttoptr (i16 8196 to ptr)
  %v3 = load volatile i16, ptr inttoptr (i16 8198 to ptr)
  %v4 = load volatile i16, ptr inttoptr (i16 8200 to ptr)
  %v5 = load volatile i16, ptr inttoptr (i16 8202 to ptr)
  call void @recur()
  store volatile i16 %v0, ptr inttoptr (i16 8224 to ptr)
  store volatile i16 %v1, ptr inttoptr (i16 8226 to ptr)
  store volatile i16 %v2, ptr inttoptr (i16 8228 to ptr)
  store volatile i16 %v3, ptr inttoptr (i16 8230 to ptr)
  store volatile i16 %v4, ptr inttoptr (i16 8232 to ptr)
  store volatile i16 %v5, ptr inttoptr (i16 8234 to ptr)
  ret void
}
