        .section .text,"ax"
        .globl __sremu

__sremu:
        ; HL = numerator, BC = divisor
        ld      d, h          ; preserve original numerator in DE
        ld      e, l

        ld      a, b
        or      c
        jr      nz, .have_divisor
        ld      hl, 0         ; div-by-zero -> 0 remainder
        ret

.have_divisor:
.__loop:
        xor     a             ; clear carry
        sbc     hl, bc        ; HL -= BC
        jr      nc, .__loop
        add     hl, bc        ; restore remainder
        ret
