; RUN: llc -mtriple=z80 -O2 < %s | FileCheck %s --check-prefix=OPT
; RUN: llc -mtriple=z80 -O0 < %s | FileCheck %s --check-prefix=NOOPT

; Basic test to ensure the pass functions in some capacity
define void @test_repeated_stores() {
; OPT-LABEL: test_repeated_stores:
; OPT:       ld hl, 4096
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_repeated_stores:
; NOOPT:     ld (4096), a
; NOOPT:     ld (4096), a
; NOOPT:     ld (4096), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4096 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4096 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4096 to i8*)
  ret void
}

; Test to make sure that it only optimizes things that it should
define void @test_different_addresses() {
; OPT-LABEL: test_different_addresses:
; OPT:       ld (4096), a
; OPT:       ld (4097), a
; OPT:       ld (4098), a

; NOOPT-LABEL: test_different_addresses:
; NOOPT:     ld (4096), a
; NOOPT:     ld (4097), a
; NOOPT:     ld (4098), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4096 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4097 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4098 to i8*)
  ret void
}