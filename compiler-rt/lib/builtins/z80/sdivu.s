	.section .text,"ax"
	.globl __sdivu

__sdivu:
	; HL = numerator, BC = divisor
	push    bc          ; preserve caller's BC
	push    de          ; preserve caller's DE
	push    af          ; preserve caller's AF

	ld      a, b
	or      c
	jr      nz, .__sdivu_have_divisor

	ld      hl, 0       ; divide by zero -> return 0
	jr      .__sdivu_restore

.__sdivu_have_divisor:
	ld      d, 0        ; quotient accumulator in DE
	ld      e, 0

.__sdivu_loop_check:
	ld      a, h
	cp      b
	jr      c, .__sdivu_done
	jr      nz, .__sdivu_subtract

	ld      a, l
	cp      c
	jr      c, .__sdivu_done

.__sdivu_subtract:
	xor     a           ; clear carry for subtraction
	sbc     hl, bc      ; HL -= divisor
	inc     de          ; quotient++
	jr      .__sdivu_loop_check

.__sdivu_done:
	ld      h, d
	ld      l, e        ; move quotient into HL

.__sdivu_restore:
	pop     af
	pop     de
	pop     bc
	ret
