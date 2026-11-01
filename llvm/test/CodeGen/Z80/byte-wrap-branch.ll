; RUN: llc -stop-after=z80-prelegalizer-combiner -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/byte-wrap-branch.py %t.s
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/byte-wrap-branch.py %t.greedy.s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/byte-wrap-branch.py %t.bank.s
; RUN: llc -regalloc=greedy -mattr=+alternate-regs -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/byte-wrap-branch.py %t.greedy-bank.s
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null
; RUN: llc -mcpu=z180 -verify-machineinstrs %s -o /dev/null

target triple = "z80"
declare {i8, i1} @llvm.uadd.with.overflow.i8(i8, i8)

define i8 @wrap(i8 %x, i8 %y) {
; MIR-LABEL: name: wrap
; MIR-NOT: G_UADDO
; MIR: G_ICMP intpred({{eq|ne}})
; CHECK-LABEL: wrap:
; CHECK: inc
  %pair = call {i8, i1} @llvm.uadd.with.overflow.i8(i8 %x, i8 1)
  %sum = extractvalue {i8, i1} %pair, 0
  %overflow = extractvalue {i8, i1} %pair, 1
  br i1 %overflow, label %wrapped, label %ordinary
wrapped:
  store volatile i8 %y, ptr inttoptr (i16 24576 to ptr)
  br label %exit
ordinary:
  store volatile i8 %x, ptr inttoptr (i16 24576 to ptr)
  br label %exit
exit:
  %r = xor i8 %sum, %y
  ret i8 %r
}

define i8 @commuted(i8 %x, i8 %y) {
; MIR-LABEL: name: commuted
; MIR-NOT: G_UADDO
; MIR: G_ICMP intpred({{eq|ne}})
; CHECK-LABEL: commuted:
; CHECK: inc
  %pair = call {i8, i1} @llvm.uadd.with.overflow.i8(i8 1, i8 %x)
  %sum = extractvalue {i8, i1} %pair, 0
  %overflow = extractvalue {i8, i1} %pair, 1
  br i1 %overflow, label %wrapped, label %ordinary
wrapped:
  store volatile i8 %y, ptr inttoptr (i16 24576 to ptr)
  br label %exit
ordinary:
  store volatile i8 %x, ptr inttoptr (i16 24576 to ptr)
  br label %exit
exit:
  %r = xor i8 %sum, %y
  ret i8 %r
}

define i8 @step_two(i8 %x, i8 %y) {
; MIR-LABEL: name: step_two
; MIR: G_UADDO
  %pair = call {i8, i1} @llvm.uadd.with.overflow.i8(i8 %x, i8 2)
  %sum = extractvalue {i8, i1} %pair, 0
  %overflow = extractvalue {i8, i1} %pair, 1
  br i1 %overflow, label %wrapped, label %ordinary
wrapped:
  store volatile i8 %y, ptr inttoptr (i16 24576 to ptr)
  br label %exit
ordinary:
  store volatile i8 %x, ptr inttoptr (i16 24576 to ptr)
  br label %exit
exit:
  %r = xor i8 %sum, %y
  ret i8 %r
}
