        .section .text,"ax"
        .globl __smulu

__smulu:
                ; BC = multiplicand, HL = multiplier
                ld      d, b          ; move multiplicand into DE for addition
                ld      e, c
                ld      b, h          ; multiplier into BC for counting
                ld      c, l
                ld      hl, 0         ; res = 0
                ld      a, b
                or      c
                ret     z             ; multiplier == 0 -> return 0
        1:
                add     hl, de         ; res += multiplicand
                dec     bc
                ld      a, b
                or      c
                jr      nz, 1b
                ret
