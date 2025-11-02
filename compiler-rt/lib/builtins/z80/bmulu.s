	.section .text,"ax"
	.globl __bmulu

__bmulu:
	; B = multiplicand, C = multiplier
	push    bc          ; preserve caller's BC
	push    de          ; preserve caller's DE

	ld      hl, 0       ; clear result
	ld      d, 0
	ld      e, b        ; multiplicand in DE (0:B)
	ld      a, c        ; multiplier in A
	ld      b, 8        ; process 8 bits

.__bmulu_loop:
	rrca                ; test lowest multiplier bit
	jr      nc, .__bmulu_skip_add
	add     hl, de      ; accumulate partial product
.__bmulu_skip_add:
	sla     e           ; multiplicand <<= 1
	rl      d
	djnz    .__bmulu_loop

	ld      a, l        ; low byte result for i8 callers

	pop     de          ; restore caller's registers
	pop     bc
	ret
