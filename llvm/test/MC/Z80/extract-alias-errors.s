; RUN: not llvm-mc -triple=z80 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

; ELF symbol values cannot express extraction of an unresolved address.
.global exported
.set exported, external>>8
; CHECK: error: cannot export Z80 extraction alias
.weak weak_alias
.set weak_alias, external&255
; CHECK: error: cannot export Z80 extraction alias
