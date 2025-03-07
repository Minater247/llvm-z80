        .section .text.function._rle_decode, "ax"
        .global _rle_decode
_rle_decode:
        ld      iy, .decode_loop
        push    ix
        ld      ix, .apply_skip
        jp      (iy)
.apply_skip:
        add     a, e                ; 1 byte, 4 t-states
        ld      e, a                ; 1 byte, 4 t-states
        jp      nc, .decode_loop    ; 3 bytes, 10 t-states
        inc     d                   ; 1 byte, 4 t-states
        jp      (iy)                ; 2 bytes, 8 t-states
        xor     a                   ;   3 0x00 16
        jp      .ld_a_16
.jump_DD1_FF2:
        ldi                        ; -122 jump_DD1_FF2
        ld       a, 0xff
        ld       (de), a
        inc      de
        ld       (de), a
        inc      de
        jp       (iy)
.jump_DD1_ZZ2:
        ldi                        ; -112 jump_DD1_ZZ2
        xor      a
        ld       (de), a
        inc      de
        ld       (de), a
        inc      de
        jp       (iy)
.jump_DD1_PDATA3:
        ldi                        ; -103 jump_DD1_PDATA3
        inc      de
        inc      de
        inc      de
        jp       (iy)
.jump_DD2_PDATA2:
        ldi                        ; -96 jump_DD2_PDATA2
        ldi
        inc      de
        inc      de
        jp       (iy)
.jump_DD1_PDATA2:
        ldi                        ; -88 jump_DD1_PDATA2
        inc      de
        inc      de
        jp       (iy)
.jump_DD2_PDATA1:
        ldi                        ; -82 jump_DD2_PDATA1
        ldi
        inc      de
        jp       (iy)
.jump_DD1_PDATA1:
        ldi                        ; -75 jump_DD1_PDATA1
        inc      de
        jp       (iy)
.first_skip:
        ld      a, 21             ; -70 -- 21
        jp      (ix)
        ld      a, 20             ; -66 -- 20
        jp      (ix)
        ld      a, 19             ; -62 -- 19
        jp      (ix)
        ld      a, 18             ; -58 -- 18
        jp      (ix)
        ld      a, 17             ; -54 -- 17
        jp      (ix)
        ld      a, 16             ; -50 -- 16
        jp      (ix)
        ld      a, 15             ; -46 -- 15
        jp      (ix)
        ld      a, 14             ; -42 -- 14
        jp      (ix)
        ld      a, 13             ; -38 -- 13
        jp      (ix)
        ld      a, 12             ; -34 -- 12
        jp      (ix)
        ld      a, 11             ; -30 -- 11
        jp      (ix)
        ld      a, 10             ; -26 -- 10
        jp      (ix)
        ld      a, 9             ; -22 -- 9
        jp      (ix)
        ld      a, 8             ; -18 -- 8
        jp      (ix)
        inc     de                 ; -14 -- 7
        inc     de                 ; -13 -- 6
        inc     de                 ; -12 -- 5
        inc     de                 ; -11 -- 4
        inc     de                 ; -10 -- 3
        inc     de                 ;  -9 -- 2
        inc     de                 ;  -8 -- 1
.decode_loop:
        ld      a, (hl)             ; Load control byte from source, 1 byte, 7 t-states
        inc     hl                  ; Advance source pointer, 1 byte, 6 t-states
        ld      (.jump + 1), a      ; 3 bytes, 13 t-states
.jump:
        jr      .post_jump          ; 2 bytes
.post_jump:
        pop     ix                  ; 0 RET
        ret                         ;
.body_end:
        .if (.body_end - .post_jump) != 3
            .error "ret offset miscalculated"
        .endif
        .if (.decode_loop - .post_jump) != -7
            .error "offset miscalculated"
        .endif
        .if (.first_skip - .post_jump) != -70
            .error "offset miscalculated"
        .endif
        .if (.jump_DD1_PDATA1 - .post_jump) != -75
            .error "offset miscalculated"
        .endif
        .if (.jump_DD2_PDATA1 - .post_jump) != -82
            .error "offset miscalculated"
        .endif
        .if (.jump_DD1_PDATA2 - .post_jump) != -88
            .error "offset miscalculated"
        .endif
        .if (.jump_DD2_PDATA2 - .post_jump) != -96
            .error "offset miscalculated"
        .endif
        .if (.jump_DD1_PDATA3 - .post_jump) != -103
            .error "offset miscalculated"
        .endif
        .if (.jump_DD1_ZZ2 - .post_jump) != -112
            .error "offset miscalculated"
        .endif
        .if (.jump_DD1_FF2 - .post_jump) != -122
            .error "offset miscalculated"
        .endif
        ld      a, 0xff             ;   3 0xFF 2
        ld      (de), a
        inc     de
        ld      a, 0xff             ;   7 0xFF 1
        ld      (de), a
        inc     de
        jp      (iy)
        ld      a, 0xff             ;  13 0xFF 3
        jp      .ld_a_3
        ld      a, 0xff             ;  18 0xFF 4
        jp      .ld_a_4
        ld      a, 0xff             ;  23 0xFF 5
        jp      .ld_a_5
        ld      a, 0xff             ;  28 0xFF 6
        jp      .ld_a_6
        ld      a, 0xff             ;  33 0xFF 7
        jp      .ld_a_7
.last_ff:
        .if (.last_ff - .post_jump) != 38
            .error "offset miscalculated"
        .endif
        xor     a                   ;  38 0x00 1
        ld      (de), a
        inc     de
        jp      (iy)
        xor     a                   ;  43 0x00 2
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        jp      (iy)
        xor     a                   ;  50 0x00 3
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        ld      (de), a
        inc     de
        jp      (iy)
        xor     a                   ;  59 0x00 4
        jp      .ld_a_4
        xor     a                   ;  63 0x00 5
        jp      .ld_a_5
        xor     a                   ;  67 0x00 6
        jp      .ld_a_6
        xor     a                   ;  71 0x00 7
        jp      .ld_a_7
        xor     a                   ;  75 0x00 8
        jp      .ld_a_8
.last_00:
        .if (.last_00 - .post_jump) != 79
            .error "offset miscalculated"
        .endif
        ldi                         ;  79 DD 10
        ldi                         ;  81 DD 9
        ldi                         ;  83 DD 8
        ldi                         ;  85 DD 7
        ldi                         ;  87 DD 6
        ldi                         ;  89 DD 5
        ldi                         ;  91 DD 4
        ldi                         ;  93 DD 3
        ldi                         ;  95 DD 2
        ldi                         ;  97 DD 1
        jp      (iy)
.last_dd:
        .if (.last_dd - .post_jump) != 101
            .error "offset miscalculated"
        .endif
        ld       a, (hl)            ; 101 RR 3
        inc      hl
        jp       .ld_a_3
        ld       a, (hl)            ; 106 RR 4
        inc      hl
        jp       .ld_a_4
        ld       a, (hl)            ; 111 RR 5
        inc      hl
        jp       .ld_a_5
        ld       a, (hl)            ; 116 RR 6
        inc      hl
        jp       .ld_a_6
        ld       a, (hl)            ; 121 RR 7
        inc      hl
        jp       .ld_a_7
        ld       a, (hl)            ; 126 RR 8
        inc      hl
        jp       .ld_a_8
.last_rr:
        .if (.last_rr - .post_jump) != 131
            .error "offset miscalculated"
        .endif
.ld_a_16:
        ld       (de), a
        inc      de
.ld_a_15:
        ld       (de), a
        inc      de
.ld_a_14:
        ld       (de), a
        inc      de
.ld_a_13:
        ld       (de), a
        inc      de
.ld_a_12:
        ld       (de), a
        inc      de
.ld_a_11:
        ld       (de), a
        inc      de
.ld_a_10:
        ld       (de), a
        inc      de
.ld_a_9:
        ld       (de), a
        inc      de
.ld_a_8:
        ld       (de), a
        inc      de
.ld_a_7:
        ld       (de), a
        inc      de
.ld_a_6:
        ld       (de), a
        inc      de
.ld_a_5:
        ld       (de), a
        inc      de
.ld_a_4:
        ld       (de), a
        inc      de
.ld_a_3:
        ld       (de), a
        inc      de
.ld_a_2:
        ld       (de), a
        inc      de
.ld_a_1:
        ld       (de), a
        inc      de
        jp       (iy)

