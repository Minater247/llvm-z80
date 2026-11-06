; RUN: %python %S/Inputs/dispatch-fields.py llc %s %t.dir
target triple = "z80"

; The raw byte is used in every handler, while decoded fields are shared by
; subsets of the switch destinations. Repeated edges name the same handler.
define i8 @dispatch(i8 %command) minsize optsize {
entry:
  %group = lshr i8 %command, 5
  %low = and i8 %command, 3
  %shift = lshr i8 %command, 2
  %port = and i8 %shift, 1
  switch i8 %group, label %other [
    i8 0, label %a
    i8 1, label %a
    i8 2, label %b
    i8 3, label %c
    i8 4, label %d
    i8 5, label %e
    i8 6, label %f
  ]
a:
  %ra = add i8 %command, %low
  ret i8 %ra
b:
  %rb = sub i8 %command, %low
  ret i8 %rb
c:
  %rc = xor i8 %command, %low
  ret i8 %rc
d:
  %rd = add i8 %command, %port
  ret i8 %rd
e:
  %re = sub i8 %command, %port
  ret i8 %re
f:
  %rf = xor i8 %command, %port
  ret i8 %rf
other:
  ret i8 %command
}

; A ladder of branches shares fields between distant handlers. Decode them
; after rejecting commands that do not consume them.
define i8 @branch_dispatch(i8 %command) minsize optsize {
entry:
  %shift = lshr i8 %command, 4
  %channel = and i8 %shift, 3
  %low = and i8 %command, 7
  %small = icmp ult i8 %command, 16
  br i1 %small, label %raw, label %second
raw:
  ret i8 %command
second:
  %medium = icmp ult i8 %command, 64
  br i1 %medium, label %a, label %third
a:
  %ra = add i8 %command, %channel
  ret i8 %ra
third:
  %large = icmp ult i8 %command, 128
  br i1 %large, label %b, label %fourth
b:
  %rb = sub i8 %command, %channel
  ret i8 %rb
fourth:
  %high = icmp ult i8 %command, 192
  br i1 %high, label %c, label %d
c:
  %rc = add i8 %command, %low
  ret i8 %rc
d:
  %rd = xor i8 %command, %low
  ret i8 %rd
}
