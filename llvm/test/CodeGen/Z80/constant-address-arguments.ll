; RUN: %python %S/Inputs/constant-address-arguments.py llc %s %t.dir
;
; Repeated internal device calls share their port addresses while memory
; barriers keep the cursor synchronized. The polling wrapper preserves the
; same hardware access order and both address arguments on its return.
target triple = "z80"

define internal fastcc void @device() nounwind {
  call void asm sideeffect "di", "~{memory}"()
  store volatile i8 39, ptr inttoptr (i16 8960 to ptr)
  %control = load i8, ptr inttoptr (i16 -16382 to ptr)
  store volatile i8 %control, ptr inttoptr (i16 8961 to ptr)
  store volatile i8 42, ptr inttoptr (i16 8960 to ptr)
  %cursor = load i16, ptr inttoptr (i16 -16384 to ptr), align 1
  %p = inttoptr i16 %cursor to ptr
  %sample = load i8, ptr %p
  store volatile i8 %sample, ptr inttoptr (i16 8961 to ptr)
  %hi = and i16 %cursor, -256
  %inc = add i16 %cursor, 1
  %lo = and i16 %inc, 255
  %next = or i16 %hi, %lo
  store i16 %next, ptr inttoptr (i16 -16384 to ptr), align 1
  call void asm sideeffect "ei", "~{memory}"()
  ret void
}

define internal fastcc void @poll() nounwind {
entry:
  br label %wait
wait:
  %status = load volatile i8, ptr inttoptr (i16 8960 to ptr)
  %bit = and i8 %status, 1
  %ready = icmp ne i8 %bit, 0
  br i1 %ready, label %write, label %wait
write:
  call fastcc void @device()
  ret void
}

define void @run(ptr %output, i8 %count) minsize optsize nounwind {
entry:
  %empty = icmp eq i8 %count, 0
  br i1 %empty, label %exit, label %loop
loop:
  %left = phi i8 [ %count, %entry ], [ %next, %loop ]
  %p = phi ptr [ %output, %entry ], [ %after, %loop ]
  call fastcc void @device()
  call fastcc void @poll()
  call fastcc void @device()
  call fastcc void @poll()
  call fastcc void @device()
  call fastcc void @poll()
  store i8 %left, ptr %p
  %after = getelementptr i8, ptr %p, i16 1
  %next = sub i8 %left, 1
  %done = icmp eq i8 %next, 0
  br i1 %done, label %exit, label %loop
exit:
  ret void
}
