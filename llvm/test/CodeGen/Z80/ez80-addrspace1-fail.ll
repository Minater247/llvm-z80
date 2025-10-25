; NOTE: Addrspace(1) loads/stores are not implemented for ez80 GISel yet.
; RUN: not --crash llc -mtriple=ez80 < %s 2>&1 | FileCheck %s --check-prefix=AS1

define i8 @load_as1(i8 addrspace(1)* %ptr) {
; AS1: Unsupported
; AS1: UNREACHABLE executed
entry:
  %val = load i8, i8 addrspace(1)* %ptr
  ret i8 %val
}
