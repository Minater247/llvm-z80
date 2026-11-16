; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/computed-byte-accesses.py llc %s %t.dir
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
;
; Byte indices may be loaded through BC/DE. Keep the scaled index in the
; adding pair, then read neighboring bytes without requiring an index register.
; CHECK-LABEL: name: lookup
; CHECK: [[BASE:%[0-9]+]]:o16 = LD16ri @table
; CHECK: $a = LD8ap
; CHECK: [[SCALED:%[0-9]+]]:a16 = ADD16aa
; CHECK: [[ADDR:%[0-9]+]]:a16 = ADD16ao [[SCALED]], [[BASE]]
; CHECK: $a = LD8ap [[ADDR]]
; CHECK: $a = LD8_ADJ [[ADDR]], 1
target triple = "z80"
@table = external global [512 x i8]

define i8 @lookup(ptr %source, i8 %initial) nounwind {
  %index = load i8, ptr %source
  %wide = zext i8 %index to i16
  %offset = shl i16 %wide, 1
  %p = getelementptr i8, ptr @table, i16 %offset
  %q = getelementptr i8, ptr %p, i16 1
  %lo = load i8, ptr %p
  %a = add i8 %lo, %initial
  store volatile i8 %a, ptr inttoptr (i16 8960 to ptr)
  %hi = load i8, ptr %q
  %b = add i8 %hi, %a
  ret i8 %b
}

; Two loaded bytes form an ordinary word, so its low byte can load directly
; to a general register even when the source address remains live.
; CHECK-LABEL: name: word_sum
; CHECK: [[LOW:%[0-9]+]]:g8 = LD8gp
; CHECK: LD8go
; CHECK: REG_SEQUENCE [[LOW]], %subreg.sub_low
; CHECK: ADD16ao
define i16 @word_sum(ptr %source, i16 %delta) {
  %lo = load volatile i8, ptr %source
  %next = getelementptr i8, ptr %source, i16 1
  %hi = load volatile i8, ptr %next
  %l = zext i8 %lo to i16
  %h = zext i8 %hi to i16
  %upper = shl i16 %h, 8
  %word = or i16 %l, %upper
  %sum = add i16 %word, %delta
  store volatile i8 0, ptr %source
  ret i16 %sum
}
