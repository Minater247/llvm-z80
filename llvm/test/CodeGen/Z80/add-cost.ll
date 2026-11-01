; RUN: opt -mtriple=z80 -passes='print<cost-model>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=Z80
; RUN: opt -mtriple=z80 -passes='print<cost-model>' -cost-kind=latency -disable-output %s 2>&1 | FileCheck %s --check-prefix=Z80
; RUN: opt -mtriple=z80 -passes='print<cost-model>' -cost-kind=code-size -disable-output %s 2>&1 | FileCheck %s --check-prefix=SIZE
; RUN: opt -mtriple=ez80 -passes='print<cost-model>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=EZ80
; RUN: opt -mtriple=z80 -mattr=+ez80 -passes='print<cost-model>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=EZ80
;
; Pair additions take longer than byte additions on the original Z80. Larger
; integers include carry propagation; their cost grows with the number of pairs.
; Z80: estimated cost of 1 for instruction: %r = add i8
; Z80: estimated cost of 3 for instruction: %r = add i16
; Z80: estimated cost of 7 for instruction: %r = add i32
; Z80: estimated cost of 15 for instruction: %r = add i64
; SIZE: estimated cost of 1 for instruction: %r = add i8
; SIZE: estimated cost of 1 for instruction: %r = add i16
; EZ80: estimated cost of 1 for instruction: %r = add i8
; EZ80: estimated cost of 1 for instruction: %r = add i16

define i8 @add8(i8 %a, i8 %b) { %r = add i8 %a, %b ret i8 %r }
define i16 @add16(i16 %a, i16 %b) { %r = add i16 %a, %b ret i16 %r }
define i32 @add32(i32 %a, i32 %b) { %r = add i32 %a, %b ret i32 %r }
define i64 @add64(i64 %a, i64 %b) { %r = add i64 %a, %b ret i64 %r }
