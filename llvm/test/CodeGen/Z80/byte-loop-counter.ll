; RUN: llc -stop-after=z80-prelegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s
; RUN: %python %S/Inputs/byte-loop-counter.py --llc llc --source %s --work %t
; RUN: sed 's/i16/i24/g;s/s16/s24/g' %s > %t.24.ll
; RUN: llc -mtriple=ez80 -stop-after=z80-prelegalizer-combiner -verify-machineinstrs %t.24.ll -o - | FileCheck %t.24.ll
; RUN: llc -mtriple=ez80 -verify-machineinstrs %t.24.ll -o /dev/null
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o /dev/null

target triple = "z80"
declare {i16, i1} @llvm.uadd.with.overflow.i16(i16, i16)

; CHECK-LABEL: name: count_1
; CHECK: G_PHI {{.*}}(s8)
define void @count_1() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -1, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_2() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -2, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_3() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -3, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_7() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -7, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_15() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -15, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_16() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -16, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_31() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -31, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_64() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -64, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; CHECK-LABEL: name: count_112
; CHECK: G_PHI {{.*}}(s8)
define void @count_112() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -112, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_127() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -127, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_128() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -128, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_129() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -129, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_254() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -254, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_255() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -255, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; CHECK-LABEL: name: count_256
; CHECK: G_PHI {{.*}}(s8)
define void @count_256() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -256, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; CHECK-LABEL: name: count_257
; CHECK: G_PHI {{.*}}(s16)
define void @count_257() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -257, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

define void @count_511() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -511, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; CHECK-LABEL: name: step_two
; CHECK: G_PHI {{.*}}(s16)
define void @step_two() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -112, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 2)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; CHECK-LABEL: name: visible_counter
; CHECK: G_PHI {{.*}}(s16)
define void @visible_counter() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -112, %entry ], [ %next, %loop ]
  store volatile i16 %count, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; CHECK-LABEL: name: continue_on_overflow
; CHECK: G_PHI {{.*}}(s16)
define void @continue_on_overflow() {
entry:
  br label %loop
loop:
  %count = phi i16 [ -1, %entry ], [ %next, %loop ]
  store volatile i8 73, ptr inttoptr (i16 24576 to ptr)
  %pair = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %count, i16 1)
  %next = extractvalue {i16, i1} %pair, 0
  %done = extractvalue {i16, i1} %pair, 1
  br i1 %done, label %loop, label %exit
exit:
  ret void
}
