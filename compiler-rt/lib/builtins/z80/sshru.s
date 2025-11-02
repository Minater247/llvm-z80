        .section .text,"ax"
        .globl __sshru

__sshru:
        ; BC = value, A = shift amount
        push    af

        or      a
        jr      z, .__sshru_done        ; no shift

.__sshru_loop:
        srl     b
        rr      c
        dec     a
        jr      nz, .__sshru_loop

.__sshru_done:
        pop af

        ret
