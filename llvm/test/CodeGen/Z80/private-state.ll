; RUN: llc -z80-promote-private-state -mattr=+alternate-regs -verify-machineinstrs %s -o %t
; RUN: FileCheck %s < %t
; RUN: %python %S/Inputs/private-state.py llc %s %t.dir
;
; A private cursor travels through repeated nested calls. An external observer
; sees synchronized memory, and an uncached caller retains the ordinary helper.
; CHECK-LABEL: advance.state:
; CHECK-NOT: (cursor)
; CHECK: exx
; CHECK: inc l
; CHECK: exx
; CHECK: ret
; CHECK-LABEL: run:
; CHECK: call pe, _setflag
; CHECK-LABEL: advance:
; CHECK: (cursor)
; CHECK: ret
target triple = "z80"
@cursor = internal global i16 0, align 1

define internal fastcc void @advance() noinline {
  %p = load i16, ptr @cursor
  %addr = inttoptr i16 %p to ptr
  %sample = load i8, ptr %addr
  store volatile i8 %sample, ptr inttoptr (i16 8960 to ptr)
  %hi = and i16 %p, -256
  %n = add i16 %p, 1
  %lo = and i16 %n, 255
  %next = or i16 %hi, %lo
  store i16 %next, ptr @cursor
  ret void
}

define internal fastcc void @group() noinline {
  call fastcc void @advance()
  call fastcc void @advance()
  call fastcc void @advance()
  call fastcc void @advance()
  ret void
}

declare void @observe()
define internal fastcc void @observe_nested() noinline {
  call void @observe()
  ret void
}
define void @run() minsize optsize {
  call fastcc void @group()
  call fastcc void @observe_nested()
  %x = load volatile i8, ptr inttoptr (i16 9216 to ptr)
  %y = load volatile i8, ptr inttoptr (i16 9217 to ptr)
  %more = icmp slt i8 %x, %y
  br i1 %more, label %extra, label %exit
extra:
  call fastcc void @group()
  br label %exit
exit:
  ret void
}

define i16 @outside() {
  call fastcc void @advance()
  %v = load i16, ptr @cursor
  ret i16 %v
}
