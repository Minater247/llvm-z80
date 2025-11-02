        .section .text,"ax"
        .globl __smulu

__smulu:
        ; BC = multiplicand, HL = multiplier
        push    de
        ld      d, h          ; multiplier into DE for counting
        ld      e, l
        ld      hl, 0         ; res = 0
        ld      a, d
        or      e
        jr      z, 2f         ; multiplier == 0 -> return 0

1:      add     hl, bc        ; res += multiplicand
        dec     de
        ld      a, d
        or      e
        jr      nz, 1b
2:      pop     de
        ret
