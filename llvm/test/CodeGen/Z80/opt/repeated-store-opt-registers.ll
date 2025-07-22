; Test for the repeated store optimization pass to ensure it correctly handles
; different source registers, doesn't assume all stores are from register A,
; and properly handles HL register conflicts
; RUN: llc -mtriple=z80 -O2 < %s | FileCheck %s --check-prefix=OPT
; RUN: llc -mtriple=z80 -O0 < %s | FileCheck %s --check-prefix=NOOPT

; Test that 8-bit stores are correctly optimized with values moved to register A
define void @test_i8_stores_from_a() {
; All i8 stores go through register A, so optimization should work
; OPT-LABEL: test_i8_stores_from_a:
; OPT:       ld hl, 4096
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_i8_stores_from_a:
; NOOPT:     ld (4096), a
; NOOPT:     ld (4096), a
; NOOPT:     ld (4096), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4096 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4096 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4096 to i8*)
  ret void
}

; Test that 16-bit stores are handled correctly when HL conflicts exist
; This test verifies that optimization is correctly skipped when HL would conflict
define void @test_i16_stores() {
; Since HL is commonly used as source for 16-bit values, optimization is prevented
; OPT-LABEL: test_i16_stores:
; OPT:       ld (8192), {{[a-z]+}}
; OPT:       ld (8192), {{[a-z]+}}

; NOOPT-LABEL: test_i16_stores:
; NOOPT:     ld (8192), {{[a-z]+}}
; NOOPT:     ld (8192), {{[a-z]+}}

entry:
  ; Store constant values - use HL as source, preventing optimization
  store volatile i16 1234, i16* inttoptr (i64 8192 to i16*)
  store volatile i16 5678, i16* inttoptr (i64 8192 to i16*)
  ret void
}

; Test mixed-size stores - same type stores should be optimized separately
define void @test_mixed_size_stores() {
; 8-bit stores may be optimized together, 16-bit store separate
; OPT-LABEL: test_mixed_size_stores:
; OPT:       ld hl, 12288
; OPT:       ld (hl), a
; OPT:       ld (hl), {{[a-z]+}}
; OPT:       ld (hl), a

; NOOPT-LABEL: test_mixed_size_stores:
; NOOPT:     ld (12288), a
; NOOPT:     ld (12288), {{[a-z]+}}
; NOOPT:     ld (12288), a

entry:
  %var = alloca i16
  %val = load i16, i16* %var
  store volatile i8 1, i8* inttoptr (i64 12288 to i8*)
  store volatile i16 %val, i16* inttoptr (i64 12288 to i16*)
  store volatile i8 2, i8* inttoptr (i64 12288 to i8*)
  ret void
}

; Test stores that should definitely be optimized - same type, same address
define void @test_consistent_i8_stores() {
; OPT-LABEL: test_consistent_i8_stores:
; OPT:       ld hl, 16384
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_consistent_i8_stores:
; NOOPT:     ld (16384), a
; NOOPT:     ld (16384), a
; NOOPT:     ld (16384), a
; NOOPT:     ld (16384), a

entry:
  store volatile i8 10, i8* inttoptr (i64 16384 to i8*)
  store volatile i8 20, i8* inttoptr (i64 16384 to i8*)
  store volatile i8 30, i8* inttoptr (i64 16384 to i8*)
  store volatile i8 40, i8* inttoptr (i64 16384 to i8*)
  ret void
}

; Test that the cost model is applied correctly
define void @test_cost_model_single_store() {
; Single stores should not be optimized
; OPT-LABEL: test_cost_model_single_store:
; OPT:       ld (20480), a

; NOOPT-LABEL: test_cost_model_single_store:
; NOOPT:     ld (20480), a

entry:
  store volatile i8 42, i8* inttoptr (i64 20480 to i8*)
  ret void
}

; Test function with multiple separate sequences
define void @test_multiple_sequences() {
; First sequence has only 2 stores and HL is live, so won't be optimized
; Second sequence has 3 stores and HL is not live, so will be optimized
; OPT-LABEL: test_multiple_sequences:
; OPT:       ld (24576), a
; OPT:       ld (24576), a
; OPT:       ld hl, 28672
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_multiple_sequences:
; NOOPT:     ld (24576), a
; NOOPT:     ld (24576), a
; NOOPT:     ld (28672), a
; NOOPT:     ld (28672), a
; NOOPT:     ld (28672), a

entry:
  ; First sequence
  store volatile i8 1, i8* inttoptr (i64 24576 to i8*)
  store volatile i8 2, i8* inttoptr (i64 24576 to i8*)
  
  ; Second sequence
  store volatile i8 3, i8* inttoptr (i64 28672 to i8*)
  store volatile i8 4, i8* inttoptr (i64 28672 to i8*)
  store volatile i8 5, i8* inttoptr (i64 28672 to i8*)
  ret void
}
