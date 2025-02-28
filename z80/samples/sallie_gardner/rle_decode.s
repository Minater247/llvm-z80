        .section .text.function._rle_decode, "ax"
        .global _rle_decode
    ; 
_rle_decode:

.decode_loop:
        ld      a, (hl)             ; Load control byte from source
        inc     hl                  ; Advance source pointer
        bit     7,a                 ; Test if zeros
        jp      nz, .decode_zeros
        bit     6,a                 ; Test if ffs
        jp      nz, .decode_ffs
        ; set up memcpy
        and     0x3f                ; Get length
        add     a, a                ; 2 bytes per LDI, so we double the value
        ld      (.memcpy_jump + 1), a
.memcpy_jump:
        jr      .memcpy_unroll
.memcpy_unroll:
        ldi                         ; First is extra
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
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        ldi
        jp      .decode_loop

.decode_zeros:
        and     0x3f                ; Get length
        add     a, a                ; 2 bytes per step, so we double the value
        ld      (.memset_jump + 1), a
        ld      a, 0x00
        jp      .memset_jump

.decode_ffs:
        and     0x3f                ; Get length
        ret     z                   ; We are done
        add     a, a                ; 2 bytes per step, so we double the value
        ld      (.memset_jump + 1), a
        ld      a, 0xFF
        jp      .memset_jump

.memset_jump:
        jr      .memset_unroll
.memset_unroll:
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

