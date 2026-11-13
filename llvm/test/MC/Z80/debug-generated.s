; RUN: llvm-mc -triple=ez80 -g -filetype=obj %s -o %t.o
; RUN: llvm-dwarfdump --verify %t.o | FileCheck %s --check-prefix=VERIFY
; RUN: llvm-dwarfdump --debug-info --debug-aranges --debug-frame %t.o | FileCheck %s

; VERIFY: No errors.
; CHECK: addr_size = 0x03
; CHECK: .debug_frame contents:
; CHECK: Address size: 3
; CHECK: Return address column: 8
; CHECK: DW_CFA_offset: IX -6
; CHECK: .debug_aranges contents:
; CHECK: length = 0x00000014
; CHECK-SAME: addr_size = 0x03
; CHECK: [0x000000, 0x000005)

.cfi_sections .debug_frame
.text
.global frame
frame:
.cfi_startproc
 push ix
.cfi_adjust_cfa_offset 3
.cfi_offset 4, -6
 pop ix
.cfi_adjust_cfa_offset -3
.cfi_restore 4
 ret
.cfi_endproc
