; RUN: %python %S/Inputs/compound-branches.py llc %s %t.dir

target triple = "z80"

; Combining comparisons in flags avoids materializing two Boolean bytes.
; Both successors contain PHIs, exercising the added and replaced edges.
define i8 @either(i8 %a, i8 %b) optsize minsize {
entry:
  %first = icmp ult i8 %a, 37
  %second = icmp eq i8 %b, 193
  %test = or i1 %first, %second
  br i1 %test, label %yes, label %no
yes:
  %y = phi i8 [ %a, %entry ]
  %yr = xor i8 %y, 85
  ret i8 %yr
no:
  %n = phi i8 [ %b, %entry ]
  %nr = add i8 %n, 11
  ret i8 %nr
}

define i8 @both(i8 %a, i8 %b) optsize minsize {
entry:
  %first = icmp uge i8 %a, 97
  %second = icmp slt i8 %b, 0
  %test = and i1 %first, %second
  br i1 %test, label %yes, label %no
yes:
  ret i8 %a
no:
  ret i8 %b
}

; Nested logic must preserve the bypass values of logical selects.
define i8 @nested(i8 %a, i8 %b) optsize minsize {
entry:
  %first = icmp eq i8 %a, 17
  %second = icmp ult i8 %b, 72
  %third = icmp uge i8 %a, 128
  %inner = select i1 %first, i1 true, i1 %second
  %test = select i1 %inner, i1 %third, i1 false
  br i1 %test, label %yes, label %no
yes:
  ret i8 23
no:
  ret i8 41
}

; A shared comparison must remain available for its other consumer.
define i8 @shared(i8 %a, i8 %b) optsize minsize {
entry:
  %first = icmp eq i8 %a, 17
  %second = icmp ult i8 %b, 72
  %test = and i1 %first, %second
  br i1 %test, label %yes, label %no
yes:
  ret i8 %a
no:
  %result = zext i1 %second to i8
  ret i8 %result
}
