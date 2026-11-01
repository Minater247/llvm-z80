; RUN: llc -verify-machineinstrs -stop-after=virtregrewriter %s -o - | FileCheck %s
; RUN: llc -regalloc=greedy -verify-machineinstrs -stop-after=virtregrewriter %s -o - | FileCheck %s
; RUN: llc -verify-machineinstrs %s -o /dev/null
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o /dev/null

target triple = "ez80"

@first = external global i16
@second = external global i16
@third = external global i16

declare void @clobber()

; Each live pair keeps its two-byte spill width while addresses use ADL mode.
; CHECK-LABEL: name: reload_pairs
; CHECK: size: 2, alignment: 1
; CHECK: LD88or %stack.{{[0-9]+}}, 0,
; CHECK: LD88ro %stack.{{[0-9]+}}, 0
define void @reload_pairs(i16 %x, i16 %y, i16 %z) {
  %a = mul i16 %x, 3
  %b = mul i16 %y, 5
  %c = mul i16 %z, 7
  call void @clobber()
  store volatile i16 %a, ptr @first
  store volatile i16 %b, ptr @second
  store volatile i16 %c, ptr @third
  ret void
}
