; RUN: %python %S/Inputs/private-memory.py llc %s %t.dir
;
; Keep a private byte across unrelated calls, publish it to a nested external
; observer, and reload the observer's update before continuing the loop.
target triple = "z80"
@cursor = internal global i8 0

define internal fastcc void @quiet() noinline nounwind {
  store volatile i8 99, ptr inttoptr (i16 9216 to ptr)
  ret void
}

declare void @observe() nounwind
define internal fastcc void @nested() noinline nounwind {
  call void @observe()
  ret void
}

define i8 @run(i8 %start, i8 %count) nounwind minsize optsize {
entry:
  store i8 %start, ptr @cursor
  %empty = icmp eq i8 %count, 0
  br i1 %empty, label %exit, label %loop
loop:
  %n = phi i8 [ %count, %entry ], [ %left, %loop ]
  %a = load i8, ptr @cursor
  %a1 = add i8 %a, 1
  store i8 %a1, ptr @cursor
  call fastcc void @quiet()
  %b = load i8, ptr @cursor
  %b1 = add i8 %b, 1
  store i8 %b1, ptr @cursor
  call fastcc void @quiet()
  %c = load i8, ptr @cursor
  %c1 = add i8 %c, 1
  store i8 %c1, ptr @cursor
  call fastcc void @quiet()
  %d = load i8, ptr @cursor
  %d1 = add i8 %d, 1
  store i8 %d1, ptr @cursor
  call fastcc void @nested()
  %left = add i8 %n, -1
  %more = icmp ne i8 %left, 0
  br i1 %more, label %loop, label %exit
exit:
  %result = load i8, ptr @cursor
  ret i8 %result
}
