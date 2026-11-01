; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/constant-multiply.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: FileCheck %s < %t.greedy.s
; RUN: %python %S/Inputs/constant-multiply.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/constant-multiply.py %t.bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mattr=+z180 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=HW

target triple = "z80"

; Small native-width products use bounded shift/add sequences. The execution
; check covers wraparound, signed bit patterns and other values live alongside
; the product, with both allocators and optional alternate registers.
; CHECK-NOT: __mulhi3
; CHECK-LABEL: mul3:
define i16 @mul3(i16 %x) minsize {
  %r = mul i16 %x, 3
  ret i16 %r
}
; CHECK-NOT: __mulhi3
; CHECK-LABEL: mul5:
define i16 @mul5(i16 %x) minsize {
  %r = mul i16 %x, 5
  ret i16 %r
}
; CHECK-NOT: __mulhi3
; CHECK-LABEL: mul7:
define i16 @mul7(i16 %x) minsize {
  %r = mul i16 %x, 7
  ret i16 %r
}
; CHECK-NOT: __mulhi3
; CHECK-LABEL: mul9:
define i16 @mul9(i16 %x) minsize {
  %r = mul i16 %x, 9
  ret i16 %r
}
; CHECK-NOT: __mulhi3
; CHECK-LABEL: mul15:
define i16 @mul15(i16 %x) minsize {
  %r = mul i16 %x, 15
  ret i16 %r
}
; CHECK-NOT: __mulhi3
; CHECK-LABEL: mul17:
define i16 @mul17(i16 %x) minsize {
  %r = mul i16 %x, 17
  ret i16 %r
}
; CHECK-NOT: __mulhi3
; CHECK-LABEL: live_products:
define i16 @live_products(i16 %x, i16 %y) {
  %a = mul i16 %x, 17
  %b = mul i16 %y, 3
  %r = add i16 %a, %b
  ret i16 %r
}
; CHECK-NOT: __mulhi3
; CHECK-LABEL: nowrap7:
define i16 @nowrap7(i16 %x) {
  %r = mul nuw i16 %x, 7
  ret i16 %r
}
; CHECK-NOT: __mulhi3

; CHECK-LABEL: byte3:
; HW-LABEL: byte3:
; HW: mlt
define i8 @byte3(i8 %x) minsize {
  %r = mul i8 %x, 3
  ret i8 %r
}

; CHECK-LABEL: byte5:
; HW-LABEL: byte5:
; HW: mlt
define i8 @byte5(i8 %x) minsize {
  %r = mul i8 %x, 5
  ret i8 %r
}

; CHECK-LABEL: byte7:
; HW-LABEL: byte7:
; HW: mlt
define i8 @byte7(i8 %x) minsize {
  %r = mul i8 %x, 7
  ret i8 %r
}

; CHECK-LABEL: byte9:
; HW-LABEL: byte9:
; HW: mlt
define i8 @byte9(i8 %x) minsize {
  %r = mul i8 %x, 9
  ret i8 %r
}

; CHECK-LABEL: byte15:
; HW-LABEL: byte15:
; HW: mlt
define i8 @byte15(i8 %x) minsize {
  %r = mul i8 %x, 15
  ret i8 %r
}

; CHECK-LABEL: byte17:
; HW-LABEL: byte17:
; HW: mlt
define i8 @byte17(i8 %x) minsize {
  %r = mul i8 %x, 17
  ret i8 %r
}

; CHECK-LABEL: mul6:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @mul6(i16 %x) minsize {
  %r = mul i16 %x, 6
  ret i16 %r
}
; CHECK-LABEL: byte6:
; HW-LABEL: byte6:
; HW: mlt
define i8 @byte6(i8 %x) minsize {
  %r = mul i8 %x, 6
  ret i8 %r
}

; CHECK-LABEL: mul10:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @mul10(i16 %x) minsize {
  %r = mul i16 %x, 10
  ret i16 %r
}
; CHECK-LABEL: byte10:
; HW-LABEL: byte10:
; HW: mlt
define i8 @byte10(i8 %x) minsize {
  %r = mul i8 %x, 10
  ret i8 %r
}

; CHECK-LABEL: mul12:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @mul12(i16 %x) minsize {
  %r = mul i16 %x, 12
  ret i16 %r
}
; CHECK-LABEL: byte12:
; HW-LABEL: byte12:
; HW: mlt
define i8 @byte12(i8 %x) minsize {
  %r = mul i8 %x, 12
  ret i8 %r
}

; CHECK-LABEL: mul14:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @mul14(i16 %x) minsize {
  %r = mul i16 %x, 14
  ret i16 %r
}
; CHECK-LABEL: byte14:
; HW-LABEL: byte14:
; HW: mlt
define i8 @byte14(i8 %x) minsize {
  %r = mul i8 %x, 14
  ret i8 %r
}

; CHECK-LABEL: mul18:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @mul18(i16 %x) minsize {
  %r = mul i16 %x, 18
  ret i16 %r
}
; CHECK-LABEL: byte18:
; HW-LABEL: byte18:
; HW: mlt
define i8 @byte18(i8 %x) minsize {
  %r = mul i8 %x, 18
  ret i8 %r
}

; CHECK-LABEL: mul20:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @mul20(i16 %x) minsize {
  %r = mul i16 %x, 20
  ret i16 %r
}
; CHECK-LABEL: byte20:
; HW-LABEL: byte20:
; HW: mlt
define i8 @byte20(i8 %x) minsize {
  %r = mul i8 %x, 20
  ret i8 %r
}

; CHECK-LABEL: mul24:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @mul24(i16 %x) minsize {
  %r = mul i16 %x, 24
  ret i16 %r
}
; CHECK-LABEL: byte24:
; HW-LABEL: byte24:
; HW: mlt
define i8 @byte24(i8 %x) minsize {
  %r = mul i8 %x, 24
  ret i8 %r
}


; CHECK-LABEL: live_even_products:
; CHECK-NOT: __mulhi3
; CHECK: ret
define i16 @live_even_products(i16 %x, i16 %y) {
  %a = mul i16 %x, 14
  %b = mul i16 %y, 24
  %sum = add i16 %a, %b
  %r = xor i16 %sum, %x
  ret i16 %r
}
