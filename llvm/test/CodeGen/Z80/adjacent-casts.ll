; RUN: %python %S/Inputs/adjacent-casts.py llc %s %t.dir
; Independent casts retain the shared identity of an integer address.
target triple = "z80"

define void @split(i16 %input, i8 %value) nounwind minsize optsize {
entry:
  %offset = and i16 %input, 6
  %first = or i16 %offset, 8960
  %second = or i16 %offset, 8961
  %p = inttoptr i16 %first to ptr
  store volatile i8 %value, ptr %p
  %v1 = add i8 %value, 1
  %v2 = add i8 %value, 2
  %condition = icmp ult i8 %value, 128
  br i1 %condition, label %left, label %right
left:
  %q1 = inttoptr i16 %second to ptr
  store volatile i8 %v1, ptr %q1
  br label %join
right:
  %q2 = inttoptr i16 %second to ptr
  store volatile i8 %v2, ptr %q2
  br label %join
join:
  %q3 = inttoptr i16 %second to ptr
  %v3 = load volatile i8, ptr %q3
  store volatile i8 %v3, ptr %p
  ret void
}

define void @single(i16 %input, i8 %value) nounwind minsize optsize {
  %offset = and i16 %input, 6
  %first = or i16 %offset, 8960
  %second = or i16 %offset, 8961
  %p = inttoptr i16 %first to ptr
  store volatile i8 %value, ptr %p
  %v1 = xor i8 %value, -1
  %q = inttoptr i16 %second to ptr
  store volatile i8 %v1, ptr %q
  ret void
}
