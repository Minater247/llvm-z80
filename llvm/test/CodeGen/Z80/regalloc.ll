; RUN: llc -mtriple=z80 -mcpu=z180 -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=NOHALVES --implicit-check-not=ixl --implicit-check-not=ixh --implicit-check-not=iyl --implicit-check-not=iyh
; NOHALVES-LABEL: pressure:
; RUN: llc -mtriple=z80 -verify-machineinstrs %s -o %t.default
; RUN: llc -mtriple=z80 -regalloc=z80 -verify-machineinstrs %s -o %t.z80
; RUN: diff %t.default %t.z80
; RUN: llc -mtriple=z80 -regalloc=greedy -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=z80 -O0 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=ez80 -O0 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=ez80-unknown-unknown-code16 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=z80 -debug-pass=Structure %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=PIPELINE
; PIPELINE-NOT: Greedy Register Allocator
; PIPELINE: Z80 Register Allocator
; PIPELINE-NEXT: {{ *}}Virtual Register Rewriter
; RUN: llc -mtriple=z80 -regalloc=greedy -debug-pass=Structure %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=GREEDY
; GREEDY: Greedy Register Allocator
; GREEDY-NOT: Z80 Register Allocator
; GREEDY: Virtual Register Rewriter
;
; Ordinary IR: no register assignments, target attributes, or inline asm.
; Check calls, regmasks, spills, PHIs, branches and multiple functions.

declare i8 @step(i8)

define i8 @across_call(i8 %a, i8 %b, i8 %c, i8 %d) {
  %x = call i8 @step(i8 %a)
  %y = add i8 %x, %b
  %z = xor i8 %y, %c
  %r = add i8 %z, %d
  ret i8 %r
}

define i8 @branch(i8 %a, i8 %b, i8 %c) {
  %test = icmp ugt i8 %a, %b
  br i1 %test, label %left, label %right
left:
  %sum = add i8 %a, %c
  br label %exit
right:
  %diff = sub i8 %b, %c
  br label %exit
exit:
  %r = phi i8 [ %sum, %left ], [ %diff, %right ]
  ret i8 %r
}

define i8 @pressure(ptr %p) {
  %a = load volatile i8, ptr %p
  %b = load volatile i8, ptr %p
  %c = load volatile i8, ptr %p
  %d = load volatile i8, ptr %p
  %e = load volatile i8, ptr %p
  %f = load volatile i8, ptr %p
  %g = load volatile i8, ptr %p
  %h = load volatile i8, ptr %p
  store volatile i8 %h, ptr %p
  store volatile i8 %g, ptr %p
  store volatile i8 %f, ptr %p
  store volatile i8 %e, ptr %p
  store volatile i8 %d, ptr %p
  store volatile i8 %c, ptr %p
  store volatile i8 %b, ptr %p
  ret i8 %a
}

define i16 @pairs(i16 %a, i16 %b, i16 %c) {
  %x = add i16 %a, %b
  %r = add i16 %x, %c
  ret i16 %r
}
