        .section .text,"ax"
        .globl __sshru

__sshru:
                ; BC = value, A = shift amount
                or      a
                ret     z             ; no shift

.__sshru_loop:
        srl     b
        rr      c
        dec     a
                jr      nz, .__sshru_loop

        ret
