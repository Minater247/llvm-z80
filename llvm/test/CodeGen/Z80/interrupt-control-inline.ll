; RUN: llc -stop-after=z80-postselect-combiner -verify-machineinstrs %s -o - | FileCheck %s
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o /dev/null
; RUN: llc -mtriple=ez80 -verify-machineinstrs %s -o /dev/null

target triple = "z80"
; CHECK-LABEL: name: disable
; CHECK: LD8
; CHECK: DI
; CHECK-NEXT: INLINEASM &"", 25
; CHECK: LD8
define i8 @disable(ptr %p, ptr %q) {
 %a = load volatile i8, ptr %p
 call void asm sideeffect " DI ", "~{memory}"()
 %b = load volatile i8, ptr %q
 %r = add i8 %a, %b
 ret i8 %r
}
; CHECK-LABEL: name: enable
; CHECK: EI
; CHECK-NEXT: INLINEASM &"", 25
define void @enable() {
 call void asm sideeffect "ei", "~{memory}"()
 ret void
}
; Extra register effects retain the explicit assembly contract.
; CHECK-LABEL: name: declared_clobber
; CHECK: INLINEASM &di,
define void @declared_clobber() {
 call void asm sideeffect "di", "~{bc},~{memory}"()
 ret void
}
; CHECK-LABEL: name: compound
; CHECK: INLINEASM
define void @compound() {
 call void asm sideeffect "ei\0Anop\0Adi", "~{memory}"()
 ret void
}
