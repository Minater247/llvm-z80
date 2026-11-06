; RUN: %python %S/Inputs/private-leaf-loop.py llc %s %t.dir
;
; A byte cursor is private even when the leaf loop writes a hardware port.
; The do-while loop executes 256 iterations for an initial count of zero.
target triple = "z80"
@cursor = internal global i8 0

define i8 @run(i8 %count) nounwind minsize optsize {
entry:
  br label %loop
loop:
  %n = phi i8 [ %count, %entry ], [ %left, %loop ]
  %old = load i8, ptr @cursor
  %next = add i8 %old, 1
  store i8 %next, ptr @cursor
  store volatile i8 %old, ptr inttoptr (i16 16384 to ptr)
  %left = add i8 %n, -1
  %more = icmp ne i8 %left, 0
  br i1 %more, label %loop, label %exit
exit:
  ret i8 %next
}
