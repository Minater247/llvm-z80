; RUN: %python %S/Inputs/adjacent-addresses.py llc %s %t.dir
;
; Repeated neighboring byte accesses share a base through allocation.
target triple = "z80"

define i16 @run(i16 %input, i8 %value) nounwind minsize optsize {
entry:
  %offset = and i16 %input, 6
  %first = or i16 %offset, 8960
  %second = or i16 %offset, 8961
  %p = inttoptr i16 %first to ptr
  %q = inttoptr i16 %second to ptr
  %v1 = add i8 %value, 1
  %v2 = add i8 %value, 2
  %v3 = add i8 %value, 3
  store volatile i8 %value, ptr %p
  store volatile i8 %v1, ptr %q
  %a = load volatile i8, ptr %p
  store volatile i8 %v2, ptr %p
  store volatile i8 %v3, ptr %q
  %b = load volatile i8, ptr %q
  %widea = zext i8 %a to i16
  %wideb = zext i8 %b to i16
  %sum = add i16 %widea, %wideb
  %result = xor i16 %sum, %first
  ret i16 %result
}
