        .section .text.function._memset,"ax"
        .global _memset
        .global __memset_nonzero

_memset:
        ; inputs: memcpy(uint8_t *dst=HL, uint16_t c=DE, uint16_t length=BC)

        ; Check if BC is zero
        ld      a, b
        or      c
        ret     z

__memset_nonzero:
        ld      (hl), e
        dec     bc

        ; check if it was memset of only one
        ld      a, b
        or      c
        ret     z

        ; set up memcpy
        ld      d, h
        ld      e, l
        inc     hl
        ex      de, hl

        jp      __memcpy_nonzero

