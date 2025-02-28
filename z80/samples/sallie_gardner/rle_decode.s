        .section .text.function._rle_decode, "ax"
        .global _rle_decode
    ; 
_rle_decode:

.decode_loop:
        ld      a, (hl)             ; Load control byte from source
        inc     hl                  ; Advance source pointer
        ld      (.cases_jump + 1), a
        ; put 0x00 or 0xFF into a as required for memset
        cp      49
        sbc     a, a
        cpl

.cases_jump:
        jr      .memcpy_unroll
.memcpy_unroll:
        ret                         ; 0
        ld      (de), a             ; 1
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        jp      .decode_loop

        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        jp      .decode_loop

        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        jp      .decode_loop

