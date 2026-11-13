; RUN: llvm-mc -triple=ez80 -filetype=obj %s -o %t.o
; RUN: llvm-dwarfdump --debug-addr --debug-aranges --debug-ranges --debug-loc %t.o | FileCheck %s

; Three-byte addresses apply to range, address and location tables, including
; the all-ones base-address marker in pre-DWARF-5 location lists.
; CHECK: .debug_loc contents:
; CHECK: [0x123410, 0x123420): DW_OP_constu 0x7
; CHECK: .debug_aranges contents:
; CHECK: addr_size = 0x03
; CHECK: [0x123400, 0x123420)
; CHECK: .debug_addr contents:
; CHECK: addr_size = 0x03
; CHECK: 0x123456
; CHECK: 0xabcdef
; CHECK: .debug_ranges contents:
; CHECK: ffffff 123400
; CHECK: 000010 000020
; CHECK: <End of list>

.section .debug_abbrev,"",@progbits
db 1, 0x11, 0, 0, 0, 0
.section .debug_info,"",@progbits
d32 9
dw 5
db 1, 3
d32 .debug_abbrev
db 1

.section .debug_aranges,"",@progbits
d32 20
dw 2
d32 0
db 3, 0
d24 0x123400, 0x20
d24 0, 0

.section .debug_ranges,"",@progbits
d24 0xffffff, 0x123400
d24 0x10, 0x20
d24 0, 0

.section .debug_loc,"",@progbits
d24 0xffffff, 0x123400
d24 0x10, 0x20
dw 2
db 0x10, 7
d24 0, 0

.section .debug_addr,"",@progbits
d32 10
dw 5
db 3, 0
d24 0x123456, 0xabcdef
