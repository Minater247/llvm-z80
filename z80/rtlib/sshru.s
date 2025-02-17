        .section .text.function.sshru,"ax"
        .global __sshru
__sshru:
        OR  A           ; Check if shift amount is zero
        RET Z           ; If zero, return immediately (BC unchanged)

.__sshru_loop:
        SRL B           ; Shift B (upper byte) right (MSB into carry)
        RR  C           ; Rotate C (lower byte) right (MSB from carry)
        DEC A           ; Decrement shift count
        JR  NZ, .__sshru_loop   ; Repeat until shift count reaches 0

        RET
