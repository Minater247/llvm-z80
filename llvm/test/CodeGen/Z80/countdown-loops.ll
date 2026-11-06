; RUN: %python %S/Inputs/countdown-loops.py llc %s %t.dir
target triple = "z80"

; The normal exit is shared with a path outside the loop. An early exit in the
; body observes the current counter, and every iteration performs ordered I/O.
define i8 @run(i8 %count) minsize optsize {
entry:
  %skip = icmp eq i8 %count, 255
  %bounded = and i8 %count, 7
  %start = add nuw i8 %bounded, 1
  br i1 %skip, label %exit, label %begin
begin:
  br label %header
header:
  %remaining = phi i8 [ %start, %begin ], [ %next, %latch ]
  %offset = phi i16 [ 0, %begin ], [ %advanced, %latch ]
  %empty = icmp eq i8 %remaining, 0
  br i1 %empty, label %exit, label %body
body:
  %out = getelementptr i8, ptr inttoptr (i16 16384 to ptr), i16 %offset
  store volatile i8 %remaining, ptr %out
  %in = getelementptr i8, ptr inttoptr (i16 12288 to ptr), i16 %offset
  %value = load volatile i8, ptr %in
  %abort = icmp eq i8 %value, 0
  br i1 %abort, label %exit, label %latch
latch:
  %next = add i8 %remaining, -1
  %advanced = add i16 %offset, 1
  br label %header
exit:
  %result = phi i8 [ 99, %entry ], [ %remaining, %header ], [ %remaining, %body ]
  ret i8 %result
}

define void @word(i16 %count) minsize optsize {
entry:
  %start = or i16 %count, 1
  br label %header
header:
  %remaining = phi i16 [ %start, %entry ], [ %next, %body ]
  %empty = icmp eq i16 %remaining, 0
  br i1 %empty, label %exit, label %body
body:
  %byte = trunc i16 %remaining to i8
  store volatile i8 %byte, ptr inttoptr (i16 16384 to ptr)
  %next = add i16 %remaining, -1
  br label %header
exit:
  ret void
}
