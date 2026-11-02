; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/nearby-pointer-arguments.py %t.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/nearby-pointer-arguments.py %t.bank.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/nearby-pointer-arguments.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

define internal fastcc void @observe(ptr %p) optsize {
  %v = load volatile i8, ptr %p
  store volatile i8 %v, ptr inttoptr (i16 8192 to ptr)
  ret void
}

; CHECK-LABEL: positive:
; CHECK-NOT: ld{{.*}}1048
; CHECK: ret
define void @positive(ptr %base) optsize {
  %p0 = getelementptr i8, ptr %base, i16 1024
  call fastcc void @observe(ptr %p0)
  %p1 = getelementptr i8, ptr %base, i16 1032
  call fastcc void @observe(ptr %p1)
  %p2 = getelementptr i8, ptr %base, i16 1040
  call fastcc void @observe(ptr %p2)
  %p3 = getelementptr i8, ptr %base, i16 1048
  call fastcc void @observe(ptr %p3)
  ret void
}

; CHECK-LABEL: negative:
; CHECK-NOT: ld{{.*}}-1000
; CHECK: ret
define void @negative(ptr %base) optsize {
  %p0 = getelementptr i8, ptr %base, i16 -1024
  call fastcc void @observe(ptr %p0)
  %p1 = getelementptr i8, ptr %base, i16 -1016
  call fastcc void @observe(ptr %p1)
  %p2 = getelementptr i8, ptr %base, i16 -1008
  call fastcc void @observe(ptr %p2)
  %p3 = getelementptr i8, ptr %base, i16 -1000
  call fastcc void @observe(ptr %p3)
  ret void
}

; CHECK-LABEL: wrap:
; CHECK-NOT: ld{{.*}}-32755
; CHECK: ret
define void @wrap(ptr %base) optsize {
  %p0 = getelementptr i8, ptr %base, i16 32760
  call fastcc void @observe(ptr %p0)
  %p1 = getelementptr i8, ptr %base, i16 32767
  call fastcc void @observe(ptr %p1)
  %p2 = getelementptr i8, ptr %base, i16 -32762
  call fastcc void @observe(ptr %p2)
  %p3 = getelementptr i8, ptr %base, i16 -32755
  call fastcc void @observe(ptr %p3)
  ret void
}
