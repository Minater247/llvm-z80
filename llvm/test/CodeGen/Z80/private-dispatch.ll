; RUN: %python %S/Inputs/private-dispatch.py llc %s %t.dir
;
; Cache a cursor through the common loop path, synchronizing a less frequent
; observer arm. Calls before and after the loop use the published global.
target triple = "z80"
@cursor = internal global i8 0

define internal fastcc void @initialize(i8 %start) noinline nounwind {
  store i8 %start, ptr @cursor
  ret void
}

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
  call fastcc void @initialize(i8 %start)
  %empty = icmp eq i8 %count, 0
  br i1 %empty, label %exit, label %begin
begin:
  br label %loop
loop:
  %n = phi i8 [ %count, %begin ], [ %left, %join ]
  %a = load i8, ptr @cursor
  %a1 = add i8 %a, 1
  store i8 %a1, ptr @cursor
  call fastcc void @quiet()
  %low = and i8 %n, 7
  %notify = icmp eq i8 %low, 0
  br i1 %notify, label %observers, label %join, !prof !0
observers:
  call fastcc void @nested()
  call fastcc void @quiet()
  call fastcc void @nested()
  call fastcc void @quiet()
  call fastcc void @nested()
  br label %join
join:
  %left = add i8 %n, -1
  %more = icmp ne i8 %left, 0
  br i1 %more, label %loop, label %exit
exit:
  call fastcc void @nested()
  ret i8 42
}

!0 = !{!"branch_weights", i32 1, i32 7}
