; NOTE: This test requires an assertions-enabled build.
; REQUIRES: asserts
; RUN: not --crash llc -mtriple=ez80 < %s 2>&1 | FileCheck %s --check-prefix=GEN
;
; Generic pointer loads currently trigger an assertion in copyPhysReg when
; expanding post-RA pseudos.  Keep the reproducer so we notice when the
; register width rules are relaxed.

define i8 @load_generic(i8* %ptr) {
; GEN: Assertion `Z80::R16RegClass.contains
; GEN: @load_generic
entry:
  %val = load i8, i8* %ptr, align 1
  ret i8 %val
}
