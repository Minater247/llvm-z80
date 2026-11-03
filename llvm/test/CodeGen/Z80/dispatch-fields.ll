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
