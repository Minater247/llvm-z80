; RUN: %python %S/Inputs/loop-masks.py llc %s %t.dir
;
; Advance a bounded byte mask instead of repeating a variable word shift.
; Empty loops and conditional consumers retain their original results.
target triple = "z80"

define i16 @run(i8 %count) nounwind minsize optsize {
entry:
  %limit = and i8 %count, 7
  %empty = icmp eq i8 %limit, 0
  br i1 %empty, label %exit, label %begin
begin:
  br label %loop
loop:
  %i = phi i8 [ 0, %begin ], [ %next, %join ]
  %sum = phi i16 [ 0, %begin ], [ %total, %join ]
  %wide = zext i8 %i to i16
  %mask = shl i16 1, %wide
  %bit = and i8 %i, 1
  %odd = icmp ne i8 %bit, 0
  br i1 %odd, label %use, label %join
use:
  %byte = trunc i16 %mask to i8
  %p = getelementptr i8, ptr inttoptr (i16 16384 to ptr), i16 %wide
  store volatile i8 %byte, ptr %p
  %added = add i16 %sum, %mask
  br label %join
join:
  %total = phi i16 [ %sum, %loop ], [ %added, %use ]
  %next = add nuw i8 %i, 1
  %more = icmp ult i8 %next, %limit
  br i1 %more, label %loop, label %exit
exit:
  %result = phi i16 [ 0, %entry ], [ %total, %join ]
  ret i16 %result
}
