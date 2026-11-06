; RUN: %python %S/Inputs/variable-memcpy.py llc %s %t.dir
target triple = "z80"

declare void @llvm.memcpy.p0.p0.i16(ptr, ptr, i16, i1 immarg)

define i16 @ends(i16 %source, i16 %dest, i16 %length) nounwind minsize optsize {
  %s = inttoptr i16 %source to ptr
  %d = inttoptr i16 %dest to ptr
  call void @llvm.memcpy.p0.p0.i16(ptr %d, ptr %s, i16 %length, i1 false)
  %se = add i16 %source, %length
  %de = add i16 %dest, %length
  %result = xor i16 %se, %de
  ret i16 %result
}

define i16 @copy(i16 %length) nounwind minsize optsize {
  call void @llvm.memcpy.p0.p0.i16(ptr inttoptr (i16 32768 to ptr),
                                  ptr inttoptr (i16 8192 to ptr),
                                  i16 %length, i1 false)
  ret i16 %length
}

define i8 @choose(i16 %length, i8 %condition) nounwind minsize optsize {
  %test = icmp eq i8 %condition, 0
  br i1 %test, label %copy, label %skip
copy:
  call void @llvm.memcpy.p0.p0.i16(ptr inttoptr (i16 32768 to ptr),
                                  ptr inttoptr (i16 8192 to ptr),
                                  i16 %length, i1 false)
  br label %exit
skip:
  br label %exit
exit:
  %result = phi i8 [ 23, %copy ], [ 41, %skip ]
  ret i8 %result
}
