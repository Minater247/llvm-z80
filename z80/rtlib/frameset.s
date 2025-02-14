        .section .text.function.frameset,"ax"
        .global __frameset
        ; push ix, set ix to current sp, move sp by input in iy.
        ; can only garble af
__frameset:
        ; this is terribly ugly and slow
        pop     af            ; take return address
        push    ix            ; save old ix
        push    af            ; re-store return address -- the coming ld ix/iy and adds garble flags so we can't keep it in af
        ; now set up new ix
        ld      ix, 2         ; 2 to take into account the currently pushed return address
        add     ix, sp        ; ix is now set to sp where old ix can be found
        ; now set up new sp
        add     iy, sp
        ld      sp, iy        ; increase stack size by given argument
        ; now we need jump to return address which is currently stored at (ix-2)
        ; lets move it to current sp (which is in iy)
        ld      a, (ix - 2)
        ld      (iy), a
        ld      a, (ix - 1)
        ld      (iy + 1), a
        ret

