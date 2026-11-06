; RUN: llc -regalloc=z80 -mattr=+alternate-regs -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -regalloc=z80 -mattr=-alternate-regs -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/regalloc-placement-regions.py llc %s %t.dir
;
; The index stays live through the record stores and the following shift.
; Reclaiming HL for the address requires moving both that byte and another
; value occupying its replacement. The complete interference component is
; larger than the whole-component search limit.
;
; CHECK-LABEL: record:
; CHECK: add hl, hl
; CHECK-NEXT: add hl, hl
; CHECK: ld (hl), a
; CHECK: inc hl
; CHECK: ld (hl), a
; CHECK: inc hl
; CHECK: ld (hl), a
; CHECK: inc hl
; CHECK: ld (hl), a
; CHECK: ret

target triple = "z80"

define void @record(i8 zeroext %index) minsize null_pointer_is_valid {
  %wide = zext i8 %index to i16
  %offset = shl nuw nsw i16 %wide, 2
  %base = inttoptr i16 %offset to ptr
  %first = load volatile i8, ptr inttoptr (i16 12584 to ptr), align 8
  %second = load volatile i8, ptr inttoptr (i16 12585 to ptr), align 1
  %p0 = getelementptr inbounds nuw i8, ptr %base, i16 300
  store volatile i8 %first, ptr %p0, align 4
  %p1 = getelementptr inbounds nuw i8, ptr %base, i16 301
  store volatile i8 %second, ptr %p1, align 1
  %third = load volatile i8, ptr inttoptr (i16 12586 to ptr), align 2
  %fourth = load volatile i8, ptr inttoptr (i16 12587 to ptr), align 1
  %p2 = getelementptr inbounds nuw i8, ptr %base, i16 302
  store volatile i8 %third, ptr %p2, align 2
  %p3 = getelementptr inbounds nuw i8, ptr %base, i16 303
  store volatile i8 %fourth, ptr %p3, align 1
  %flags0 = load volatile i8, ptr inttoptr (i16 12304 to ptr), align 16
  %bit = shl nuw i16 1, %wide
  %byte = trunc i16 %bit to i8
  %mask = xor i8 %byte, -1
  %cleared0 = and i8 %flags0, %mask
  store volatile i8 %cleared0, ptr inttoptr (i16 12304 to ptr), align 16
  %flags1 = load volatile i8, ptr inttoptr (i16 12305 to ptr), align 1
  %cleared1 = and i8 %flags1, %mask
  store volatile i8 %cleared1, ptr inttoptr (i16 12305 to ptr), align 1
  ret void
}
