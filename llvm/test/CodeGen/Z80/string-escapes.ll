; RUN: llc -mtriple=z80 -filetype=asm < %s | FileCheck %s

@spn = internal constant [4 x i8] c"|/-\\"

define i8* @spn_ptr() {
entry:
  %ptr = getelementptr inbounds [4 x i8], [4 x i8]* @spn, i16 0, i16 0
  ret i8* %ptr
}

; CHECK: _spn:
; CHECK-NEXT: db "|/-\\"
