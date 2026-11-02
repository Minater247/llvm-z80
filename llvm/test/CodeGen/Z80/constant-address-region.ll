; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/constant-address-region.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/constant-address-region.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/constant-address-region.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

; Polling and subsequent byte writes share a primary pair. The caller's
; incoming address remains available across the call without spilling.
; CHECK-LABEL: poll:
; CHECK: ld hl, 8192
; CHECK: bit 0, (hl)
; CHECK-NOT: ld hl, 8192
; CHECK: ld (hl), 39
; CHECK: ld (hl), 41
; CHECK: ld (hl), 42
; CHECK: ret
define internal fastcc void @poll() optsize {
entry:
  br label %wait
wait:
  %status = load volatile i8, ptr inttoptr (i16 8192 to ptr)
  %bit = and i8 %status, 1
  %busy = icmp eq i8 %bit, 0
  br i1 %busy, label %wait, label %write
write:
  store volatile i8 39, ptr inttoptr (i16 8192 to ptr)
  store volatile i8 41, ptr inttoptr (i16 8192 to ptr)
  store volatile i8 42, ptr inttoptr (i16 8192 to ptr)
  ret void
}

; CHECK-LABEL: caller:
; CHECK-NOT: push de
; CHECK: call poll
; CHECK: ld a, (de)
; CHECK: ret
define i8 @caller(ptr %p) optsize {
  call fastcc void @poll()
  %v = load i8, ptr %p
  ret i8 %v
}
