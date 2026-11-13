; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=ez80 -filetype=obj %s -o %t.ez80.o
; RUN: llvm-readobj -r %t.ez80.o | FileCheck %s --check-prefix=ADL
; RUN: llc -stop-after=z80-prelegalizer-combiner %s -o - | FileCheck %s --check-prefix=MIR
;
; Field addresses outside the indexed displacement range are relocatable
; constants, including when the base is also used. Avoid runtime arithmetic
; and make the address independently rematerializable across calls and loops.
target triple = "z80"
@record = external global [640 x i8]

declare void @consume(ptr)

; CHECK-LABEL: field:
; CHECK: ld hl, record+338
; CHECK-NOT: add hl,
; CHECK: ret
; OBJ: R_Z80_16 record 0x152
; ADL: R_Z80_24 record 0x152
; MIR-LABEL: name: field
; MIR: G_GLOBAL_VALUE @record + 338
; MIR-NOT: G_PTR_ADD
; MIR: RET
define ptr @field() {
  %p = getelementptr i8, ptr @record, i16 338
  ret ptr %p
}

; CHECK-LABEL: base_and_field:
; CHECK: ld {{[a-z]+}}, record
; CHECK: call consume
; CHECK: ld hl, record+294
; CHECK-NOT: add hl,
; CHECK: ret
; MIR-LABEL: name: base_and_field
; MIR-DAG: G_GLOBAL_VALUE @record
; MIR-DAG: G_GLOBAL_VALUE @record + 294
; MIR-NOT: G_PTR_ADD
; MIR: RET
define ptr @base_and_field() {
  call void @consume(ptr @record)
  %p = getelementptr i8, ptr @record, i16 294
  ret ptr %p
}

; CHECK-LABEL: chained:
; CHECK: ld hl, record+300
; CHECK-NOT: add hl,
; CHECK: ret
; MIR-LABEL: name: chained
; MIR: G_GLOBAL_VALUE @record + 300
; MIR-NOT: G_PTR_ADD
; MIR: RET
define ptr @chained() {
  %p = getelementptr i8, ptr @record, i16 338
  %q = getelementptr i8, ptr %p, i16 -38
  ret ptr %q
}

; The last byte belongs to the object and remains representable.
; CHECK-LABEL: last:
; CHECK: ld hl, record+639
; CHECK: ret
; MIR-LABEL: name: last
; MIR: G_GLOBAL_VALUE @record + 639
; MIR-NOT: G_PTR_ADD
; MIR: RET
define ptr @last() {
  %p = getelementptr i8, ptr @record, i16 639
  ret ptr %p
}

; One-past can wrap when the object occupies the final byte of memory.
; MIR-LABEL: name: end
; MIR: G_PTR_ADD
; MIR: RET
define ptr @end() {
  %p = getelementptr i8, ptr @record, i16 640
  ret ptr %p
}

; An out-of-object offset may wrap the address space at runtime. Keep it
; out of the absolute relocation, which would instead report an overflow.
; MIR-LABEL: name: outside
; MIR: G_PTR_ADD
; MIR: RET
define ptr @outside() {
  %p = getelementptr i8, ptr @record, i16 641
  ret ptr %p
}

; MIR-LABEL: name: variable
; MIR: G_PTR_ADD
; MIR: RET
define ptr @variable(i16 %offset) {
  %p = getelementptr i8, ptr @record, i16 %offset
  ret ptr %p
}
