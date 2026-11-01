; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s

target triple = "ez80"

@function_address = constant ptr @target
@offset_address = constant ptr getelementptr (i8, ptr @function_address, i24 3)
@absolute = constant i24 1193046

; CHECK-LABEL: function_address:
; CHECK-NEXT: d24 target
; CHECK-LABEL: offset_address:
; CHECK-NEXT: d24 function_address+3
; CHECK-LABEL: absolute:
; CHECK-NEXT: d24 1193046

declare void @target()
