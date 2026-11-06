; RUN: %python %S/Inputs/regional-leaf.py llc %s %t.dir
; Repeated small leaves share their cursor and constant addresses in a region.
target triple = "z80"
@cursor = internal global i16 0

define internal fastcc void @step() nounwind minsize optsize {
entry:
  br label %poll
poll:
  %ready = load volatile i8, ptr inttoptr (i16 8960 to ptr)
  %bit = and i8 %ready, 1
  %waiting = icmp eq i8 %bit, 0
  br i1 %waiting, label %poll, label %output
output:
  %position = load i16, ptr @cursor
  %source = inttoptr i16 %position to ptr
  %sample = load i8, ptr %source
  store volatile i8 39, ptr inttoptr (i16 8960 to ptr)
  store volatile i8 17, ptr inttoptr (i16 8961 to ptr)
  store volatile i8 42, ptr inttoptr (i16 8960 to ptr)
  store volatile i8 %sample, ptr inttoptr (i16 8961 to ptr)
  %page = and i16 %position, -256
  %step = add i16 %position, 1
  %low = and i16 %step, 255
  %next = or i16 %page, %low
  store i16 %next, ptr @cursor
  ret void
}

define internal fastcc void @region() nounwind minsize optsize {
  call fastcc void @step()
  call fastcc void @step()
  call fastcc void @step()
  call fastcc void @step()
  call fastcc void @step()
  call fastcc void @step()
  ret void
}

define i16 @drive(i16 %start) nounwind minsize optsize {
  store i16 %start, ptr @cursor
  call fastcc void @region()
  %next = load i16, ptr @cursor
  ret i16 %next
}
