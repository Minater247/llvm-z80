; RUN: llc -stop-after=instruction-select -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/accumulator-sum-store.py llc %s %t.dir
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
;
; A stored byte sum already passes through A. Its address may use BC/DE,
; leaving HL available for the next source access.
target triple = "z80"

define i8 @run(ptr %source, ptr %output) nounwind {
; CHECK-LABEL: name: run
; CHECK: ADD8ar
; CHECK: LD8pa
; CHECK: LD8pa
  %x = load i8, ptr %source
  %next = getelementptr i8, ptr %source, i16 1
  %y = load i8, ptr %next
  %sum = add i8 %x, %y
  store volatile i8 %sum, ptr %output
  %z = load i8, ptr %next
  %again = add i8 %sum, %z
  %address = ptrtoint ptr %output to i16
  %low = trunc i16 %address to i8
  %newlow = add i8 %low, 1
  %page = and i16 %address, -256
  %wide = zext i8 %newlow to i16
  %newaddress = or i16 %page, %wide
  %dest = inttoptr i16 %newaddress to ptr
  store volatile i8 %again, ptr %dest
  ret i8 %again
}
