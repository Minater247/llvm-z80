        .section .text.function.sshl,"ax"
        .global __sshl
        ; Input: HL = value, A = shift count
        ; Output: HL = shifted value
__sshl:
        or a          ; Check if shift count is 0
        ret z         ; If zero, return immediately (HL unchanged)

.__sshl_loop:
        add hl, hl    ; hl = 2 * hl = hl << 1
        dec a         ; Decrement shift count
        jp nz, .__sshl_loop  ; Continue looping if A > 0

        ret           ; Return with result in HL
