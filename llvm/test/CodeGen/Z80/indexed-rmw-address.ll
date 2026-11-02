; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/indexed-rmw-address.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/indexed-rmw-address.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/indexed-rmw-address.py %t.greedy.s

target triple = "z80"

; The load and store merge into one address consumer. Preserve an incoming
; index across the conditional field update, including displacement endpoints.
; CHECK-LABEL: update:
; CHECK-NOT: add iy,
; CHECK: inc (iy + 23)
; CHECK-NOT: add iy,
; CHECK: dec (iy - 128)
; CHECK-NOT: add iy,
; CHECK: set 5, (iy + 127)
; CHECK: ret
define fastcc void @update(ptr %p, i8 %c) optsize {
  %test = icmp eq i8 %c, 0
  br i1 %test, label %end, label %change
change:
  %a = getelementptr i8, ptr %p, i16 23
  %av = load i8, ptr %a
  %ai = add i8 %av, 1
  store i8 %ai, ptr %a
  %b = getelementptr i8, ptr %p, i16 -128
  %bv = load i8, ptr %b
  %bi = add i8 %bv, -1
  store i8 %bi, ptr %b
  %d = getelementptr i8, ptr %p, i16 127
  %dv = load i8, ptr %d
  %di = or i8 %dv, 32
  store i8 %di, ptr %d
  br label %end
end:
  ret void
}

; A caller can retain its index across the helper after IPRA sees that the
; indexed updates preserve it. This catches reintroducing pointer arithmetic.
define i16 @caller(ptr %p, i8 %c) optsize {
  call fastcc void @update(ptr %p, i8 %c)
  %v = ptrtoint ptr %p to i16
  ret i16 %v
}
