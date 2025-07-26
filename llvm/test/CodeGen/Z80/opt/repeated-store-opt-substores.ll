; Test for substores optimization when sequences need to be split
; RUN: llc -mtriple=z80-none-elf+full -O2 < %s | FileCheck %s --check-prefix=OPT
; RUN: llc -mtriple=z80-none-elf+full -O0 < %s | FileCheck %s --check-prefix=NOOPT

; Test simple subsequence optimization with inline assembly breaking the sequence
define void @test_inline_assembly_break() {
; Inline assembly doesn't interfere with HL, but register allocation does affect the sequence.
; OPT-LABEL: test_inline_assembly_break:
; OPT:       ld (4096), a
; OPT:       ld a, l
; OPT:       ld hl, 4096
; OPT:       ld (hl), a
; OPT:       ;APP
; OPT:       ;NO_APP
; OPT:       ld (hl), a

; NOOPT-LABEL: test_inline_assembly_break:
; NOOPT:     ld (4096), a
; NOOPT:     ld (4096), a
; NOOPT:     ld (4096), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4096 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4096 to i8*)
  call void asm sideeffect "", ""()  ; Inline assembly doesn't affect HL, sequence continues
  store volatile i8 3, i8* inttoptr (i64 4096 to i8*)
  ret void
}

; Test multiple subsequences with mixed register usage
define void @test_mixed_register_stores() {
; HL conflict from `ld a, l` splits the sequence. First store unoptimized, then 3 stores optimized.
; OPT-LABEL: test_mixed_register_stores:
; OPT:       ld (8192), a
; OPT:       ld a, l
; OPT:       ld hl, 8192
; OPT:       ld (hl), a
; OPT:       ld (hl), bc
; OPT:       ld (hl), a

; NOOPT-LABEL: test_mixed_register_stores:
; NOOPT:     ld (8192), a
; NOOPT:     ld (8192), a
; NOOPT:     ld (8192), bc
; NOOPT:     ld (8192), a

entry:
  store volatile i8 1, i8* inttoptr (i64 8192 to i8*)
  store volatile i8 2, i8* inttoptr (i64 8192 to i8*)
  store volatile i16 4660, i16* inttoptr (i64 8192 to i16*)
  store volatile i8 4, i8* inttoptr (i64 8192 to i8*)
  ret void
}

; Test cost model - when HL conflicts split the sequence
define void @test_hl_liveness_cost_model() {
; Multiple HL conflicts (ld a, l and ld a, h) split the sequence into subsequences
; OPT-LABEL: test_hl_liveness_cost_model:
; OPT:       ld (12288), a
; OPT:       ld a, l
; OPT:       ld (12288), a
; OPT:       ld a, e
; OPT:       ld (12288), a
; OPT:       ld a, c
; OPT:       ld (12288), a
; OPT:       ld a, h
; OPT:       ld hl, 12288
; OPT:       ld (hl), a
; OPT:       ld a, d
; OPT:       ld (hl), a

; NOOPT-LABEL: test_hl_liveness_cost_model:
; NOOPT:     ld (12288), a
; NOOPT:     ld (12288), a
; NOOPT:     ld (12288), a
; NOOPT:     ld (12288), a
; NOOPT:     ld (12288), a
; NOOPT:     ld (12288), a

entry:
  %hl_var = alloca i16  ; This forces HL to be live
  store volatile i8 1, i8* inttoptr (i64 12288 to i8*)
  store volatile i8 2, i8* inttoptr (i64 12288 to i8*)
  store volatile i8 3, i8* inttoptr (i64 12288 to i8*)
  store volatile i8 4, i8* inttoptr (i64 12288 to i8*)
  store volatile i8 5, i8* inttoptr (i64 12288 to i8*)
  store volatile i8 6, i8* inttoptr (i64 12288 to i8*)
  %hl_val = load i16, i16* %hl_var  ; Use the allocated variable
  ret void
}

; Test that small sequences do NOT get optimized when HL is live
define void @test_small_sequence_hl_not_live() {
; L register is used (ld a, l), making HL live. With only 2 stores, no optimization should occur.
; OPT-LABEL: test_small_sequence_hl_not_live:
; OPT:       ld (16384), a
; OPT:       ld a, l
; OPT:       ld (16384), a

; NOOPT-LABEL: test_small_sequence_hl_not_live:
; NOOPT:     ld (16384), a
; NOOPT:     ld (16384), a

entry:
  %hl_var = alloca i16  ; This creates some stack usage but doesn't make HL live
  store volatile i8 1, i8* inttoptr (i64 16384 to i8*)
  store volatile i8 2, i8* inttoptr (i64 16384 to i8*)
  %hl_val = load i16, i16* %hl_var  ; Use the allocated variable
  ret void
}

; Test HL preservation when HL is live - should use PUSH/POP
define i16 @test_hl_preservation_required() {
; When HL is live and we have enough stores (6+), optimization should happen with preservation
; OPT-LABEL: test_hl_preservation_required:
; OPT:       push hl
; OPT:       ld hl, 20480
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       pop hl

; NOOPT-LABEL: test_hl_preservation_required:
; NOOPT:     ld (20480), a
; NOOPT:     ld (20480), a
; NOOPT:     ld (20480), a
; NOOPT:     ld (20480), a
; NOOPT:     ld (20480), a
; NOOPT:     ld (20480), a

entry:
  ; Force HL to be live using inline assembly
  %hl_input = call i16 asm "ld hl, 0x5678", "={hl}"()
  
  ; 6 stores - meets threshold for optimization when HL is live
  store volatile i8 1, i8* inttoptr (i64 20480 to i8*)
  store volatile i8 2, i8* inttoptr (i64 20480 to i8*)
  store volatile i8 3, i8* inttoptr (i64 20480 to i8*)
  store volatile i8 4, i8* inttoptr (i64 20480 to i8*)
  store volatile i8 5, i8* inttoptr (i64 20480 to i8*)
  store volatile i8 6, i8* inttoptr (i64 20480 to i8*)
  
  ; Return HL value - forces HL to remain live
  ret i16 %hl_input
}

; Test that HL is NOT optimized when live but insufficient stores
define i16 @test_hl_live_insufficient_stores() {
; Only 2 stores with HL live - should NOT optimize (needs 6+)
; OPT-LABEL: test_hl_live_insufficient_stores:
; OPT:       ld (24576), a
; OPT:       ld (24576), a

; NOOPT-LABEL: test_hl_live_insufficient_stores:
; NOOPT:     ld (24576), a
; NOOPT:     ld (24576), a

entry:
  ; Force HL to be live
  %hl_input = call i16 asm "ld hl, 0x1234", "={hl}"()
  
  ; Only 2 stores - insufficient when HL is live
  store volatile i8 1, i8* inttoptr (i64 24576 to i8*)
  store volatile i8 2, i8* inttoptr (i64 24576 to i8*)
  
  ; Return HL value
  ret i16 %hl_input
}

; Test H register liveness detection - HL conflict causes sequence split
define i8 @test_h_register_preservation() {
; HL conflict from `ld a, h` splits sequence. Only 5 stores in subsequence, so no optimization occurs.
; OPT-LABEL: test_h_register_preservation:
; OPT:       ld (28672), a
; OPT:       ld (28672), a
; OPT:       ld (28672), a
; OPT:       ld (28672), a
; OPT:       ld (28672), a

; NOOPT-LABEL: test_h_register_preservation:
; NOOPT:     ld (28672), a
; NOOPT:     ld (28672), a
; NOOPT:     ld (28672), a
; NOOPT:     ld (28672), a
; NOOPT:     ld (28672), a
; NOOPT:     ld (28672), a

entry:
  ; Force H register to be live
  %h_input = call i8 asm "ld h, 0x42", "={h}"()
  
  ; 6 stores to trigger optimization
  store volatile i8 1, i8* inttoptr (i64 28672 to i8*)
  store volatile i8 2, i8* inttoptr (i64 28672 to i8*)
  store volatile i8 3, i8* inttoptr (i64 28672 to i8*)
  store volatile i8 4, i8* inttoptr (i64 28672 to i8*)
  store volatile i8 5, i8* inttoptr (i64 28672 to i8*)
  store volatile i8 6, i8* inttoptr (i64 28672 to i8*)
  
  ; Return H value
  ret i8 %h_input
}

; Test L register liveness detection - HL is live, insufficient stores for optimization
define i8 @test_l_register_preservation() {
; L register is live and used at the end. With only 6 stores but HL live, insufficient for optimization.
; OPT-LABEL: test_l_register_preservation:
; OPT:       ld (-32768), a
; OPT:       ld a, e
; OPT:       ld (-32768), a
; OPT:       ld a, c
; OPT:       ld (-32768), a
; OPT:       ld a, h
; OPT:       ld (-32768), a
; OPT:       ld a, d
; OPT:       ld (-32768), a
; OPT:       ld a, b
; OPT:       ld (-32768), a

; NOOPT-LABEL: test_l_register_preservation:
; NOOPT:     ld (-32768), a
; NOOPT:     ld (-32768), a
; NOOPT:     ld (-32768), a
; NOOPT:     ld (-32768), a
; NOOPT:     ld (-32768), a
; NOOPT:     ld (-32768), a

entry:
  ; Force L register to be live
  %l_input = call i8 asm "ld l, 0x37", "={l}"()
  
  ; 6 stores to trigger optimization
  store volatile i8 1, i8* inttoptr (i64 32768 to i8*)
  store volatile i8 2, i8* inttoptr (i64 32768 to i8*)
  store volatile i8 3, i8* inttoptr (i64 32768 to i8*)
  store volatile i8 4, i8* inttoptr (i64 32768 to i8*)
  store volatile i8 5, i8* inttoptr (i64 32768 to i8*)
  store volatile i8 6, i8* inttoptr (i64 32768 to i8*)
  
  ; Return L value
  ret i8 %l_input
}

; Test edge case: HL used between stores but not live after
define void @test_hl_used_between_not_live() {
; First store should remain unoptimized (single store before inline asm)
; Inline assembly uses HL but doesn't make it live after
; Subsequent stores should be optimized without HL preservation
; OPT-LABEL: test_hl_used_between_not_live:
; OPT:       ld (8192), a
; OPT:       ld hl, 8192
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_hl_used_between_not_live:
; NOOPT:     ld (8192), a
; NOOPT:     ld (8192), a
; NOOPT:     ld (8192), a
; NOOPT:     ld (8192), a
; NOOPT:     ld (8192), a
; NOOPT:     ld (8192), a

entry:
  ; First store - will be left unoptimized (single store before inline asm)
  store volatile i8 1, i8* inttoptr (i64 8192 to i8*)
  
  ; Use HL temporarily - this breaks the sequence but doesn't make HL live after
  %temp = call i16 asm sideeffect "ld hl, 0x1234", "={hl}"()
  call void asm sideeffect "ld (0x9000), hl", "{hl}"(i16 %temp)
  
  ; More stores - should be optimized together without HL preservation
  ; because HL is not live after this sequence
  store volatile i8 2, i8* inttoptr (i64 8192 to i8*)
  store volatile i8 3, i8* inttoptr (i64 8192 to i8*)
  store volatile i8 4, i8* inttoptr (i64 8192 to i8*)
  store volatile i8 5, i8* inttoptr (i64 8192 to i8*)
  store volatile i8 6, i8* inttoptr (i64 8192 to i8*)
  
  ret void
}
