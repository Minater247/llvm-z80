; Reuse adjacent cursors without keeping a second live copy of the base.
; Exercise full-width carry/borrow and independent low-byte wrapping.
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/cursor-chain.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/cursor-chain.py %t.greedy.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"

define void @pointer_inc(ptr %base, ptr %out) {
  %p1 = getelementptr i8, ptr %base, i16 1
  %out1 = getelementptr i8, ptr %out, i16 0
  store ptr %p1, ptr %out1
  %p2 = getelementptr i8, ptr %base, i16 2
  %out2 = getelementptr i8, ptr %out, i16 2
  store ptr %p2, ptr %out2
  %p3 = getelementptr i8, ptr %base, i16 3
  %out3 = getelementptr i8, ptr %out, i16 4
  store ptr %p3, ptr %out3
  %p4 = getelementptr i8, ptr %base, i16 4
  %out4 = getelementptr i8, ptr %out, i16 6
  store ptr %p4, ptr %out4
  %saved = getelementptr i8, ptr %out, i16 8
  store ptr %base, ptr %saved
  ret void
}

define void @pointer_dec(ptr %base, ptr %out) {
  %p1 = getelementptr i8, ptr %base, i16 -1
  %out1 = getelementptr i8, ptr %out, i16 0
  store ptr %p1, ptr %out1
  %p2 = getelementptr i8, ptr %base, i16 -2
  %out2 = getelementptr i8, ptr %out, i16 2
  store ptr %p2, ptr %out2
  %p3 = getelementptr i8, ptr %base, i16 -3
  %out3 = getelementptr i8, ptr %out, i16 4
  store ptr %p3, ptr %out3
  %p4 = getelementptr i8, ptr %base, i16 -4
  %out4 = getelementptr i8, ptr %out, i16 6
  store ptr %p4, ptr %out4
  %saved = getelementptr i8, ptr %out, i16 8
  store ptr %base, ptr %saved
  ret void
}

define void @byte_inc(ptr %base, ptr %out) {
  %p1 = getelementptr i8, ptr %base, i16 1
  %b1 = ptrtoint ptr %p1 to i8
  %out1 = getelementptr i8, ptr %out, i16 0
  store i8 %b1, ptr %out1
  %p2 = getelementptr i8, ptr %base, i16 2
  %b2 = ptrtoint ptr %p2 to i8
  %out2 = getelementptr i8, ptr %out, i16 1
  store i8 %b2, ptr %out2
  %p3 = getelementptr i8, ptr %base, i16 3
  %b3 = ptrtoint ptr %p3 to i8
  %out3 = getelementptr i8, ptr %out, i16 2
  store i8 %b3, ptr %out3
  %p4 = getelementptr i8, ptr %base, i16 4
  %b4 = ptrtoint ptr %p4 to i8
  %out4 = getelementptr i8, ptr %out, i16 3
  store i8 %b4, ptr %out4
  %saved = getelementptr i8, ptr %out, i16 8
  store ptr %base, ptr %saved
  ret void
}

define void @byte_dec(ptr %base, ptr %out) {
  %p1 = getelementptr i8, ptr %base, i16 -1
  %b1 = ptrtoint ptr %p1 to i8
  %out1 = getelementptr i8, ptr %out, i16 0
  store i8 %b1, ptr %out1
  %p2 = getelementptr i8, ptr %base, i16 -2
  %b2 = ptrtoint ptr %p2 to i8
  %out2 = getelementptr i8, ptr %out, i16 1
  store i8 %b2, ptr %out2
  %p3 = getelementptr i8, ptr %base, i16 -3
  %b3 = ptrtoint ptr %p3 to i8
  %out3 = getelementptr i8, ptr %out, i16 2
  store i8 %b3, ptr %out3
  %p4 = getelementptr i8, ptr %base, i16 -4
  %b4 = ptrtoint ptr %p4 to i8
  %out4 = getelementptr i8, ptr %out, i16 3
  store i8 %b4, ptr %out4
  %saved = getelementptr i8, ptr %out, i16 8
  store ptr %base, ptr %saved
  ret void
}

define void @page_inc(ptr %base, ptr %out) {
  %bits = ptrtoint ptr %base to i16
  %high = and i16 %bits, -256
  %p1 = getelementptr i8, ptr %base, i16 1
  %bits1 = ptrtoint ptr %p1 to i16
  %low1 = and i16 %bits1, 255
  %page1 = or i16 %high, %low1
  %out1 = getelementptr i8, ptr %out, i16 0
  store i16 %page1, ptr %out1
  %p2 = getelementptr i8, ptr %base, i16 2
  %bits2 = ptrtoint ptr %p2 to i16
  %low2 = and i16 %bits2, 255
  %page2 = or i16 %high, %low2
  %out2 = getelementptr i8, ptr %out, i16 2
  store i16 %page2, ptr %out2
  %p3 = getelementptr i8, ptr %base, i16 3
  %bits3 = ptrtoint ptr %p3 to i16
  %low3 = and i16 %bits3, 255
  %page3 = or i16 %high, %low3
  %out3 = getelementptr i8, ptr %out, i16 4
  store i16 %page3, ptr %out3
  %p4 = getelementptr i8, ptr %base, i16 4
  %bits4 = ptrtoint ptr %p4 to i16
  %low4 = and i16 %bits4, 255
  %page4 = or i16 %high, %low4
  %out4 = getelementptr i8, ptr %out, i16 6
  store i16 %page4, ptr %out4
  %saved = getelementptr i8, ptr %out, i16 8
  store ptr %base, ptr %saved
  ret void
}

define void @page_dec(ptr %base, ptr %out) {
  %bits = ptrtoint ptr %base to i16
  %high = and i16 %bits, -256
  %p1 = getelementptr i8, ptr %base, i16 -1
  %bits1 = ptrtoint ptr %p1 to i16
  %low1 = and i16 %bits1, 255
  %page1 = or i16 %high, %low1
  %out1 = getelementptr i8, ptr %out, i16 0
  store i16 %page1, ptr %out1
  %p2 = getelementptr i8, ptr %base, i16 -2
  %bits2 = ptrtoint ptr %p2 to i16
  %low2 = and i16 %bits2, 255
  %page2 = or i16 %high, %low2
  %out2 = getelementptr i8, ptr %out, i16 2
  store i16 %page2, ptr %out2
  %p3 = getelementptr i8, ptr %base, i16 -3
  %bits3 = ptrtoint ptr %p3 to i16
  %low3 = and i16 %bits3, 255
  %page3 = or i16 %high, %low3
  %out3 = getelementptr i8, ptr %out, i16 4
  store i16 %page3, ptr %out3
  %p4 = getelementptr i8, ptr %base, i16 -4
  %bits4 = ptrtoint ptr %p4 to i16
  %low4 = and i16 %bits4, 255
  %page4 = or i16 %high, %low4
  %out4 = getelementptr i8, ptr %out, i16 6
  store i16 %page4, ptr %out4
  %saved = getelementptr i8, ptr %out, i16 8
  store ptr %base, ptr %saved
  ret void
}
