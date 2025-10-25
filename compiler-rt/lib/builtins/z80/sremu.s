        .section .text,"ax"
        .globl __sremu

__sremu:
        ld      a, b          ; check divisor (BC)
        or      c
        jr      nz, .have_divisor
        ld      hl, 0
        ret

.have_divisor:
.loop:
        xor     a
        sbc     hl, bc
        jr      nc, .loop
        add     hl, bc
        ret
