; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
;
; Global symbols have the same absolute byte addressing modes as numeric
; constants. Materialize a pair when nearby accesses amortize it, and use
; absolute addressing for isolated accesses separated by a call or loop.
target triple = "z80"
@state = external global i8

declare void @consume(i8)

; CHECK-LABEL: across_call:
; CHECK-NOT: ld hl, state
; CHECK: ld a, (state)
; CHECK: call consume
; CHECK-NOT: ld hl, state
; CHECK: ld a, (state)
; CHECK: ret
define i8 @across_call() {
  %a = load volatile i8, ptr @state
  call void @consume(i8 %a)
  %b = load volatile i8, ptr @state
  ret i8 %b
}

; CHECK-LABEL: short_region:
; CHECK: ld hl, state
; CHECK: ld (hl), 3
; CHECK: ld (hl), 5
; CHECK: ld (hl), 7
; CHECK: ret
define void @short_region() {
  store volatile i8 3, ptr @state
  store volatile i8 5, ptr @state
  store volatile i8 7, ptr @state
  ret void
}

; CHECK-LABEL: across_loop:
; CHECK-NOT: ld hl, state
; CHECK: ld a, (state)
; CHECK-NOT: ld hl, state
; CHECK: ld (state), a
; CHECK: ret
define void @across_loop(ptr %output, i8 %count) {
entry:
  %initial = load i8, ptr @state
  br label %loop
loop:
  %value = phi i8 [ %initial, %entry ], [ %next, %loop ]
  %left = phi i8 [ %count, %entry ], [ %remaining, %loop ]
  store volatile i8 %value, ptr %output
  %next = add i8 %value, 1
  %remaining = sub i8 %left, 1
  %done = icmp eq i8 %remaining, 0
  br i1 %done, label %exit, label %loop
exit:
  store i8 %next, ptr @state
  ret void
}

; A distant use does not prevent sharing the address within a short region.
; CHECK-LABEL: region_and_call:
; CHECK: ld hl, state
; CHECK: ld (hl), 3
; CHECK: ld (hl), 5
; CHECK: ld (hl), 7
; CHECK: call consume
; CHECK-NOT: ld hl, state
; CHECK: ld a, (state)
; CHECK: ret
define i8 @region_and_call() {
  store volatile i8 3, ptr @state
  store volatile i8 5, ptr @state
  store volatile i8 7, ptr @state
  call void @consume(i8 0)
  %value = load volatile i8, ptr @state
  ret i8 %value
}

; A read-modify-write needs the address for both its load and its store.
; Share that pair with an adjacent read even after the update is fused.
; CHECK-LABEL: read_and_update:
; CHECK-NOT: ld a, (state)
; CHECK: ld hl, state
; CHECK: ld {{[a-z]+}}, (hl)
; CHECK: inc (hl)
; CHECK: ret
define i8 @read_and_update() {
  %old = load volatile i8, ptr @state
  %value = load volatile i8, ptr @state
  %next = add i8 %value, 1
  store volatile i8 %next, ptr @state
  ret i8 %old
}

; Recreating an absolute address lets pair arithmetic use HL directly.
; CHECK-LABEL: arithmetic_between_accesses:
; CHECK: call consume
; CHECK-NOT: ld hl, 356
; CHECK: ld a, (356)
; CHECK: add hl,
; CHECK-NOT: ld hl, 356
; CHECK: ld (356), a
; CHECK: ld a, (356)
; CHECK: ret
define i8 @arithmetic_between_accesses(i16 %step) {
  call void @consume(i8 0)
  %lo = load volatile i8, ptr inttoptr (i16 355 to ptr)
  %hi = load volatile i8, ptr inttoptr (i16 356 to ptr)
  %lo16 = zext i8 %lo to i16
  %hi16 = zext i8 %hi to i16
  %upper = shl i16 %hi16, 8
  %value = or i16 %lo16, %upper
  %sum = add i16 %value, %step
  %newlo = trunc i16 %sum to i8
  %shifted = lshr i16 %sum, 8
  %newhi = trunc i16 %shifted to i8
  store volatile i8 %newlo, ptr inttoptr (i16 355 to ptr)
  store volatile i8 %newhi, ptr inttoptr (i16 356 to ptr)
  %result = load volatile i8, ptr inttoptr (i16 356 to ptr)
  ret i8 %result
}
