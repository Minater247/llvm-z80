        .section .text.function._memcpy,"ax"
        .global _memcpy, __memcpy_nonzero
        .global __memcpy32, __memcpy31, __memcpy30, __memcpy29, __memcpy28, __memcpy27, __memcpy26, __memcpy25
        .global __memcpy24, __memcpy23, __memcpy22, __memcpy21, __memcpy20, __memcpy19, __memcpy18, __memcpy17
        .global __memcpy16, __memcpy15, __memcpy14, __memcpy13, __memcpy12, __memcpy11, __memcpy10, __memcpy09
        .global __memcpy08, __memcpy07, __memcpy06, __memcpy05, __memcpy04, __memcpy03, __memcpy02, __memcpy01
        .global __memcpy32_jr

        ; non-C-standard: this memcpy does not return destination address
        ; argments: dst=DE, src=HL, count=BC XXX not correct: need ex de, hl right now
        ; clobbers: af
        ; uses inline block of LDI-s (16 t-states per byte) and not LDIR (21 t-states per byte)
        ; self-modifying to jump to right place of LDI-s
_memcpy:
        ex      de, hl ; XXX still old libcall API
        ; first check if bc is 0 (zero copy)
        ld      a, b
        or      c
        ret     z
__memcpy_nonzero:
        ; self-modifying code, not ROM or multithreading compliant
        ld      a, c
        and     a, 0x1f
        jp      z, __memcpy32       ; already multiple of 32, jump to ldi-s
        add     a, a                ; ldi is two bytes, so must double the offset
        cpl
        add     a, 0x41
        ld      (__memcpy32_jr + 1), a
__memcpy32_jr:
        jr      __memcpy32
        ; setup is 61 t-states, break even vs just LDIR at ~61/(21-16)=12 bytes.

__memcpy32: ldi
__memcpy31: ldi
__memcpy30: ldi
__memcpy29: ldi
__memcpy28: ldi
__memcpy27: ldi
__memcpy26: ldi
__memcpy25: ldi
__memcpy24: ldi
__memcpy23: ldi
__memcpy22: ldi
__memcpy21: ldi
__memcpy20: ldi
__memcpy19: ldi
__memcpy18: ldi
__memcpy17: ldi
__memcpy16: ldi
__memcpy15: ldi
__memcpy14: ldi
__memcpy13: ldi
__memcpy12: ldi
__memcpy11: ldi
__memcpy10: ldi
__memcpy09: ldi
__memcpy08: ldi
__memcpy07: ldi
__memcpy06: ldi
__memcpy05: ldi
__memcpy04: ldi
__memcpy03: ldi
__memcpy02: ldi
__memcpy01: ldi
__memcpy00:
        ; we assume most copies are short, so try to return first
        ;jp      pe, __memcpy32
        ret     po
        jp      __memcpy32

