; RUN: %python %S/Inputs/bank-addresses.py llc %s %t.dir
;
; Carry invariant addresses and a mutable cursor through nested calls.
; The leaf polls and writes in one bank activation across its loop.
target triple = "z80"
@cursor = internal global i16 12544, align 1
@timer = internal global i8 21

define internal fastcc void @emit() noinline nounwind {
entry:
  br label %poll
poll:
  %status = load volatile i8, ptr inttoptr (i16 8960 to ptr)
  %bit = and i8 %status, 1
  %ready = icmp ne i8 %bit, 0
  br i1 %ready, label %body, label %poll
body:
  store volatile i8 39, ptr inttoptr (i16 8960 to ptr)
  %control = load i8, ptr @timer
  store volatile i8 %control, ptr inttoptr (i16 8961 to ptr)
  store volatile i8 42, ptr inttoptr (i16 8960 to ptr)
  %p = load i16, ptr @cursor
  %addr = inttoptr i16 %p to ptr
  %sample = load i8, ptr %addr
  store volatile i8 %sample, ptr inttoptr (i16 8961 to ptr)
  %hi = and i16 %p, -256
  %n = add i16 %p, 1
  %lo = and i16 %n, 255
  %next = or i16 %hi, %lo
  store i16 %next, ptr @cursor
  ret void
}
define internal fastcc void @group() noinline nounwind {
  call fastcc void @emit()
  call fastcc void @emit()
  call fastcc void @emit()
  call fastcc void @emit()
  ret void
}
define void @run() minsize optsize nounwind {
  call fastcc void @group()
  call fastcc void @group()
  ret void
}
define i16 @outside() nounwind {
  call fastcc void @emit()
  %v = load i16, ptr @cursor
  ret i16 %v
}
