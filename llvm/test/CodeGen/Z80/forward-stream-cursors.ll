; RUN: %python %S/Inputs/forward-stream-cursors.py llc %s %t.dir
target triple = "z80"

; The induction points at the last byte. The second block completes a group;
; an early return observes an address from the partially consumed group.
define i16 @stream(i16 %source, i8 %groups, i8 %abort) minsize optsize {
entry:
  %empty = icmp eq i8 %groups, 0
  br i1 %empty, label %none, label %begin
none:
  ret i16 %source
begin:
  %initial = add i16 %source, 3
  br label %header
header:
  %cursor = phi i16 [ %initial, %begin ], [ %next, %body ]
  %remaining = phi i8 [ %groups, %begin ], [ %decrement, %body ]
  %address0 = add i16 %cursor, -3
  %pointer0 = inttoptr i16 %address0 to ptr
  %value0 = load volatile i8, ptr %pointer0
  store volatile i8 %value0, ptr inttoptr (i16 16384 to ptr)
  %address1 = add i16 %cursor, -2
  %pointer1 = inttoptr i16 %address1 to ptr
  %value1 = load volatile i8, ptr %pointer1
  store volatile i8 %value1, ptr inttoptr (i16 16384 to ptr)
  %stop = icmp ne i8 %abort, 0
  br i1 %stop, label %early, label %body
early:
  %early_address = add i16 %cursor, -1
  ret i16 %early_address
body:
  %base = inttoptr i16 %cursor to ptr
  %pointer2 = getelementptr i8, ptr %base, i16 -1
  %value2 = load volatile i8, ptr %pointer2
  store volatile i8 %value2, ptr inttoptr (i16 16384 to ptr)
  %pointer3 = inttoptr i16 %cursor to ptr
  %value3 = load volatile i8, ptr %pointer3
  store volatile i8 %value3, ptr inttoptr (i16 16384 to ptr)
  %next = add i16 %cursor, 4
  %decrement = add i8 %remaining, -1
  %done = icmp eq i8 %decrement, 0
  br i1 %done, label %exit, label %header
exit:
  %last_group = add i16 %cursor, -2
  ret i16 %last_group
}
