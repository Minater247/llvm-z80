; RUN: llc -mtriple=z80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
;
; An early-clobber output must not overlap the input, even though the input
; dies in this instruction. Inline asm is used only to test this constraint;
; the ordinary-C benchmark does not use it.
define i8 @early_clobber(i8 %a, i8 %b) {
  %x = call i8 asm sideeffect "", "=&r,r"(i8 %a)
  %r = add i8 %x, %b
  ret i8 %r
}
