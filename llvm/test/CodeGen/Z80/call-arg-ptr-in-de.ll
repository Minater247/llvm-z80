; NOTE: Assertions intentionally hand-written to lock in calling convention and store selection.
; RUN: llc -mtriple=z80 < %s | FileCheck %s --check-prefixes=Z80
; RUN: llc -mtriple=ez80-code16 < %s | FileCheck %s --check-prefixes=EZ80-CODE16

; A source global of type i8* so its symbol type is i8**
@src = external global i8*
@global2 = external global i8**

; Callee: must use argument in DE directly and store with LD (nn),DE (no HL shuffle)
; Z80-LABEL: sink:
; Z80:       ; %bb.0:
; Z80-NEXT:    ld (_global2), de
; Z80-NEXT:    ret
;
; EZ80-CODE16-LABEL: sink:
; EZ80-CODE16:       ; %bb.0:
; EZ80-CODE16-NEXT:    ld (_global2), de
; EZ80-CODE16-NEXT:    ret
define dso_local void @sink(i8** %p) nounwind noinline "frame-pointer"="none" {
entry:
  store i8** %p, i8*** @global2
  ret void
}

; Caller: must materialize @src into DE and emit a direct 'call _sink'
; Z80-LABEL: caller:
; Z80:       ; %bb.0:
; Z80:         ld de, _src
; Z80-NEXT:    call _sink
; Z80-NEXT:    ret
;
; EZ80-CODE16-LABEL: caller:
; EZ80-CODE16:       ; %bb.0:
; EZ80-CODE16:         ld de, _src
; EZ80-CODE16-NEXT:    call _sink
; EZ80-CODE16-NEXT:    ret
define dso_local void @caller() nounwind "frame-pointer"="none" {
entry:
  call void @sink(i8** @src)
  ret void
}
