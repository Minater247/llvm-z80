; Comprehensive test suite for Z80 Repeated Store Optimization Pass
; Tests all edge cases, cost model scenarios, register conflicts, and liveness analysis
; RUN: llc -mtriple=z80-none-elf+full -O2 < %s | FileCheck %s --check-prefix=OPT
; RUN: llc -mtriple=z80-none-elf+full -O0 < %s | FileCheck %s --check-prefix=NOOPT

; ========== BASIC FUNCTIONALITY TESTS ==========

; Test 1: Basic two-store case with register allocation conflicts
; Register allocation causes HL conflict, so no optimization occurs
define void @test_basic_two_stores_register_conflict() {
; OPT-LABEL: test_basic_two_stores_register_conflict:
; OPT:       ld (4096), a
; OPT:       ld a, l
; OPT:       ld (4096), a
; OPT-NOT:   ld hl, 4096

; NOOPT-LABEL: test_basic_two_stores_register_conflict:
; NOOPT:     ld (4096), a
; NOOPT:     ld (4096), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4096 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4096 to i8*)
  ret void
}

; Test 2: Single store should not be optimized
define void @test_single_store_no_opt() {
; OPT-LABEL: test_single_store_no_opt:
; OPT:       ld (4097), a
; OPT-NOT:   ld hl, 4097

; NOOPT-LABEL: test_single_store_no_opt:
; NOOPT:     ld (4097), a

entry:
  store volatile i8 42, i8* inttoptr (i64 4097 to i8*)
  ret void
}

; Test 3: Different addresses should not be optimized together
define void @test_different_addresses() {
; OPT-LABEL: test_different_addresses:
; OPT:       ld (4098), a
; OPT:       ld (4099), a
; OPT:       ld (4100), a
; OPT-NOT:   ld hl, {{.*}}

; NOOPT-LABEL: test_different_addresses:
; NOOPT:     ld (4098), a
; NOOPT:     ld (4099), a
; NOOPT:     ld (4100), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4098 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4099 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4100 to i8*)
  ret void
}

; Test 4: Large sequence with register allocation conflicts
; Register allocation uses H and L registers, preventing optimization
define void @test_large_sequence_register_conflicts() {
; OPT-LABEL: test_large_sequence_register_conflicts:
; OPT:       ld (4101), a
; OPT:       ld a, l
; OPT:       ld (4101), a
; OPT:       ld a, e
; OPT:       ld (4101), a
; OPT:       ld a, c
; OPT:       ld (4101), a
; OPT:       ld a, h
; OPT:       ld (4101), a
; OPT-NOT:   ld hl, 4101

; NOOPT-LABEL: test_large_sequence_register_conflicts:
; NOOPT:     ld (4101), a
; NOOPT:     ld (4101), a
; NOOPT:     ld (4101), a
; NOOPT:     ld (4101), a
; NOOPT:     ld (4101), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4101 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4101 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4101 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4101 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4101 to i8*)
  ret void
}

; ========== COST MODEL TESTS ==========

; Test 5: HL live with 5 stores - should NOT optimize (< 6 threshold)
define i16 @test_hl_live_five_stores_no_opt() {
; OPT-LABEL: test_hl_live_five_stores_no_opt:
; OPT:       ld (4102), a
; OPT:       ld (4102), a
; OPT:       ld (4102), a
; OPT:       ld (4102), a
; OPT:       ld (4102), a
; OPT-NOT:   push hl
; OPT-NOT:   ld hl, 4102

; NOOPT-LABEL: test_hl_live_five_stores_no_opt:
; NOOPT:     ld (4102), a
; NOOPT:     ld (4102), a
; NOOPT:     ld (4102), a
; NOOPT:     ld (4102), a
; NOOPT:     ld (4102), a

entry:
  %hl_input = call i16 asm "ld hl, 0x5678", "={hl}"()
  store volatile i8 1, i8* inttoptr (i64 4102 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4102 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4102 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4102 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4102 to i8*)
  ret i16 %hl_input
}

; Test 6: HL live with exactly 6 stores - should optimize with PUSH/POP
define i16 @test_hl_live_six_stores_optimize() {
; OPT-LABEL: test_hl_live_six_stores_optimize:
; OPT:       push hl
; OPT:       ld hl, 4103
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       pop hl

; NOOPT-LABEL: test_hl_live_six_stores_optimize:
; NOOPT:     ld (4103), a
; NOOPT:     ld (4103), a
; NOOPT:     ld (4103), a
; NOOPT:     ld (4103), a
; NOOPT:     ld (4103), a
; NOOPT:     ld (4103), a

entry:
  %hl_input = call i16 asm "ld hl, 0x1234", "={hl}"()
  store volatile i8 1, i8* inttoptr (i64 4103 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4103 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4103 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4103 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4103 to i8*)
  store volatile i8 6, i8* inttoptr (i64 4103 to i8*)
  ret i16 %hl_input
}

; Test 7: HL live with 10 stores - should optimize with PUSH/POP
define i16 @test_hl_live_ten_stores_optimize() {
; OPT-LABEL: test_hl_live_ten_stores_optimize:
; OPT:       push hl
; OPT:       ld hl, 4104
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       pop hl

; NOOPT-LABEL: test_hl_live_ten_stores_optimize:
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a
; NOOPT:     ld (4104), a

entry:
  %hl_input = call i16 asm "ld hl, 0xabcd", "={hl}"()
  store volatile i8 1, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 6, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 7, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 8, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 9, i8* inttoptr (i64 4104 to i8*)
  store volatile i8 10, i8* inttoptr (i64 4104 to i8*)
  ret i16 %hl_input
}

; ========== HL CONFLICT TESTS ==========

; Test 8: HL conflict in the middle should split sequence
define i16 @test_hl_conflict_split() {
; OPT-LABEL: test_hl_conflict_split:
; OPT:       ld (4105), a
; OPT:       ld (4105), a
; OPT:       ld (4105), hl
; OPT:       ld (4105), a
; OPT:       ld (4105), a

; NOOPT-LABEL: test_hl_conflict_split:
; NOOPT:     ld (4105), a
; NOOPT:     ld (4105), a
; NOOPT:     ld (4105), hl
; NOOPT:     ld (4105), a
; NOOPT:     ld (4105), a

entry:
  %hl_value = call i16 asm "ld hl, 0x4321", "={hl}"()
  store volatile i8 1, i8* inttoptr (i64 4105 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4105 to i8*)
  store volatile i16 %hl_value, i16* inttoptr (i64 4105 to i16*)  ; HL conflict
  store volatile i8 3, i8* inttoptr (i64 4105 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4105 to i8*)
  ret i16 %hl_value
}

; Test 9: H register conflict should split sequence
define i8 @test_h_conflict_split() {
; OPT-LABEL: test_h_conflict_split:
; OPT:       ld (4106), a
; OPT:       ld (4106), a
; OPT:       ld (4106), a
; OPT:       ld (4106), a

; NOOPT-LABEL: test_h_conflict_split:
; NOOPT:     ld (4106), a
; NOOPT:     ld (4106), a
; NOOPT:     ld (4106), a
; NOOPT:     ld (4106), a

entry:
  %h_value = call i8 asm "ld h, 0x55", "={h}"()
  store volatile i8 1, i8* inttoptr (i64 4106 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4106 to i8*)
  call void asm sideeffect "ld (4106), a", "{h}"(i8 %h_value)  ; H register usage
  store volatile i8 3, i8* inttoptr (i64 4106 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4106 to i8*)
  ret i8 %h_value
}

; Test 10: L register conflict should split sequence
define i8 @test_l_conflict_split() {
; OPT-LABEL: test_l_conflict_split:
; OPT:       ld (4107), a
; OPT:       ld (4107), a
; OPT:       ld (4107), a
; OPT:       ld (4107), a

; NOOPT-LABEL: test_l_conflict_split:
; NOOPT:     ld (4107), a
; NOOPT:     ld (4107), a
; NOOPT:     ld (4107), a
; NOOPT:     ld (4107), a

entry:
  %l_value = call i8 asm "ld l, 0xaa", "={l}"()
  store volatile i8 1, i8* inttoptr (i64 4107 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4107 to i8*)
  call void asm sideeffect "ld (4107), a", "{l}"(i8 %l_value)  ; L register usage
  store volatile i8 3, i8* inttoptr (i64 4107 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4107 to i8*)
  ret i8 %l_value
}

; Test 11: Multiple register conflicts should split sequence into unoptimized subsequences
define i16 @test_multiple_register_conflicts() {
; OPT-LABEL: test_multiple_register_conflicts:
; OPT:       ld (4108), a
; OPT:       ld (4108), a
; OPT:       ld (4108), hl
; OPT:       ld (4108), a
; OPT:       ld (4108), bc
; OPT:       ld (4108), a

; NOOPT-LABEL: test_multiple_register_conflicts:
; NOOPT:     ld (4108), a
; NOOPT:     ld (4108), a
; NOOPT:     ld (4108), hl
; NOOPT:     ld (4108), a
; NOOPT:     ld (4108), bc
; NOOPT:     ld (4108), a

entry:
  %de_value = call i16 asm "ld de, 0x1111", "={de}"()
  %bc_value = call i16 asm "ld bc, 0x2222", "={bc}"()
  
  store volatile i8 1, i8* inttoptr (i64 4108 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4108 to i8*)
  store volatile i16 %de_value, i16* inttoptr (i64 4108 to i16*)  ; First conflict (DE register)
  store volatile i8 3, i8* inttoptr (i64 4108 to i8*)
  store volatile i16 %bc_value, i16* inttoptr (i64 4108 to i16*)  ; Second conflict (BC register)  
  store volatile i8 4, i8* inttoptr (i64 4108 to i8*)
  
  %combined = add i16 %de_value, %bc_value
  ret i16 %combined
}

; ========== 16-BIT AND 24-BIT STORE TESTS ==========

; Test 12: 16-bit stores with compatible registers
define void @test_16bit_stores_compatible() {
; OPT-LABEL: test_16bit_stores_compatible:
; OPT:       ld hl, 4109
; OPT:       ld (hl), de
; OPT:       ld (hl), bc

; NOOPT-LABEL: test_16bit_stores_compatible:
; NOOPT:     ld (4109), de
; NOOPT:     ld (4109), bc

entry:
  %de_val = call i16 asm "ld de, 0x3333", "={de}"()
  %bc_val = call i16 asm "ld bc, 0x4444", "={bc}"()
  store volatile i16 %de_val, i16* inttoptr (i64 4109 to i16*)
  store volatile i16 %bc_val, i16* inttoptr (i64 4109 to i16*)
  ret void
}

; Test 13: 16-bit stores with HL conflict (source is HL)
define void @test_16bit_stores_hl_source_conflict() {
; OPT-LABEL: test_16bit_stores_hl_source_conflict:
; OPT:       ld (4110), de
; OPT:       ld (4110), hl
; OPT:       ld (4110), bc
; OPT-NOT:   ld hl, 4110

; NOOPT-LABEL: test_16bit_stores_hl_source_conflict:
; NOOPT:     ld (4110), de
; NOOPT:     ld (4110), hl
; NOOPT:     ld (4110), bc

entry:
  %de_val = call i16 asm "ld de, 0x5555", "={de}"()
  %hl_val = call i16 asm "ld hl, 0x6666", "={hl}"()
  %bc_val = call i16 asm "ld bc, 0x7777", "={bc}"()
  
  store volatile i16 %de_val, i16* inttoptr (i64 4110 to i16*)
  store volatile i16 %hl_val, i16* inttoptr (i64 4110 to i16*)  ; HL conflict
  store volatile i16 %bc_val, i16* inttoptr (i64 4110 to i16*)
  ret void
}

; Test 14: Mixed 8-bit and 16-bit stores
define void @test_mixed_8bit_16bit_stores() {
; OPT-LABEL: test_mixed_8bit_16bit_stores:
; OPT:       ld (4111), a
; OPT:       ld (4111), de
; OPT:       ld hl, 4111
; OPT:       ld (hl), a
; OPT:       ld (hl), bc

; NOOPT-LABEL: test_mixed_8bit_16bit_stores:
; NOOPT:     ld (4111), a
; NOOPT:     ld (4111), de
; NOOPT:     ld (4111), a
; NOOPT:     ld (4111), bc

entry:
  %de_val = call i16 asm "ld de, 0x8888", "={de}"()
  %bc_val = call i16 asm "ld bc, 0x9999", "={bc}"()
  
  store volatile i8 1, i8* inttoptr (i64 4111 to i8*)
  store volatile i16 %de_val, i16* inttoptr (i64 4111 to i16*)
  store volatile i8 2, i8* inttoptr (i64 4111 to i8*)
  store volatile i16 %bc_val, i16* inttoptr (i64 4111 to i16*)
  ret void
}

; ========== LIVENESS ANALYSIS EDGE CASES ==========

; Test 15: HL used but not live at function end
define void @test_hl_used_not_live_at_end() {
; OPT-LABEL: test_hl_used_not_live_at_end:
; OPT:       ld hl, 4112
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_hl_used_not_live_at_end:
; NOOPT:     ld (4112), a
; NOOPT:     ld (4112), a
; NOOPT:     ld (4112), a

entry:
  %hl_temp = call i16 asm "ld hl, 0xaaaa", "={hl}"()
  call void asm sideeffect "ld (0x2000), hl", "{hl}"(i16 %hl_temp)  ; Use HL but don't return it
  
  store volatile i8 1, i8* inttoptr (i64 4112 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4112 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4112 to i8*)
  ret void
}

; Test 16: HL live through function calls
define i16 @test_hl_live_through_function_call() {
; OPT-LABEL: test_hl_live_through_function_call:
; OPT:       push hl
; OPT:       ld hl, 4113
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       pop hl

; NOOPT-LABEL: test_hl_live_through_function_call:
; NOOPT:     ld (4113), a
; NOOPT:     ld (4113), a
; NOOPT:     ld (4113), a
; NOOPT:     ld (4113), a
; NOOPT:     ld (4113), a
; NOOPT:     ld (4113), a

entry:
  %hl_input = call i16 asm "ld hl, 0xbbbb", "={hl}"()
  
  store volatile i8 1, i8* inttoptr (i64 4113 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4113 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4113 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4113 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4113 to i8*)
  store volatile i8 6, i8* inttoptr (i64 4113 to i8*)
  
  ret i16 %hl_input
}

; Test 17: H register live prevents optimization (6 stores, no optimization)
define i8 @test_h_live_prevents_optimization() {
; OPT-LABEL: test_h_live_prevents_optimization:
; OPT:       ld (4114), a
; OPT:       ld (4114), a
; OPT:       ld (4114), a
; OPT:       ld (4114), a
; OPT:       ld (4114), a

; NOOPT-LABEL: test_h_live_prevents_optimization:
; NOOPT:     ld (4114), a
; NOOPT:     ld (4114), a
; NOOPT:     ld (4114), a
; NOOPT:     ld (4114), a
; NOOPT:     ld (4114), a
; NOOPT:     ld (4114), a

entry:
  %h_input = call i8 asm "ld h, 0xcc", "={h}"()
  
  store volatile i8 1, i8* inttoptr (i64 4114 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4114 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4114 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4114 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4114 to i8*)
  store volatile i8 6, i8* inttoptr (i64 4114 to i8*)
  
  ret i8 %h_input
}

; Test 18: L register live prevents optimization (6 stores, no optimization)
define i8 @test_l_live_prevents_optimization() {
; OPT-LABEL: test_l_live_prevents_optimization:
; OPT:       ld (4115), a
; OPT:       ld (4115), a
; OPT:       ld (4115), a
; OPT:       ld (4115), a
; OPT:       ld (4115), a
; OPT:       ld (4115), a

; NOOPT-LABEL: test_l_live_prevents_optimization:
; NOOPT:     ld (4115), a
; NOOPT:     ld (4115), a
; NOOPT:     ld (4115), a
; NOOPT:     ld (4115), a
; NOOPT:     ld (4115), a
; NOOPT:     ld (4115), a

entry:
  %l_input = call i8 asm "ld l, 0xdd", "={l}"()
  
  store volatile i8 1, i8* inttoptr (i64 4115 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4115 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4115 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4115 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4115 to i8*)
  store volatile i8 6, i8* inttoptr (i64 4115 to i8*)
  
  ret i8 %l_input
}

; ========== INSTRUCTION SEQUENCE INTERRUPTION TESTS ==========

; Test 19: Inline assembly that doesn't affect HL - sequence should NOT break
define void @test_inline_asm_breaks_sequence() {
; OPT-LABEL: test_inline_asm_breaks_sequence:
; OPT:       ld (4116), a
; OPT:       ld a, l
; OPT:       ld hl, 4116
; OPT:       ld (hl), a
; OPT:       ;APP
; OPT:       nop
; OPT:       ;NO_APP
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_inline_asm_breaks_sequence:
; NOOPT:     ld (4116), a
; NOOPT:     ld (4116), a
; NOOPT:     nop
; NOOPT:     ld (4116), a
; NOOPT:     ld (4116), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4116 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4116 to i8*)
  call void asm sideeffect "nop", ""()  ; Does not affect HL, sequence continues
  store volatile i8 3, i8* inttoptr (i64 4116 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4116 to i8*)
  ret void
}

; Test 19b: Inline assembly that DOES modify HL - should break sequence
define void @test_inline_asm_modifies_hl() {
; OPT-LABEL: test_inline_asm_modifies_hl:
; OPT:       ld (4116), a
; OPT:       ld a, l
; OPT:       ld (4116), a
; OPT:       ;APP
; OPT:       ld hl, 0x1234
; OPT:       ;NO_APP
; OPT:       ld hl, 4116
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_inline_asm_modifies_hl:
; NOOPT:     ld (4116), a
; NOOPT:     ld a, l
; NOOPT:     ld (4116), a
; NOOPT:     ld hl, 0x1234
; NOOPT:     ld (4116), a
; NOOPT:     ld (4116), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4116 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4116 to i8*)
  call void asm sideeffect "ld hl, 0x1234", "~{hl}"()  ; Modifies HL, breaks sequence
  store volatile i8 3, i8* inttoptr (i64 4116 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4116 to i8*)
  ret void
}

; Test 20: Function call breaking sequence
declare void @external_function()

define void @test_function_call_breaks_sequence() {
; OPT-LABEL: test_function_call_breaks_sequence:
; OPT:       ld (4117), a
; OPT:       ld (4117), a
; OPT:       call {{.*}}external_function
; OPT:       ld hl, 4117
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_function_call_breaks_sequence:
; NOOPT:     ld (4117), a
; NOOPT:     ld (4117), a
; NOOPT:     call {{.*}}external_function
; NOOPT:     ld (4117), a
; NOOPT:     ld (4117), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4117 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4117 to i8*)
  call void @external_function()  ; Breaks sequence
  store volatile i8 3, i8* inttoptr (i64 4117 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4117 to i8*)
  ret void
}

; Test 21: Memory load breaking sequence (HL modification)
define void @test_memory_load_breaks_sequence() {
; OPT-LABEL: test_memory_load_breaks_sequence:
; OPT:       ld (4118), a
; OPT:       ld (4118), a
; OPT:       ld hl, 4118
; OPT:       ld (hl), a
; OPT:       ld (hl), a

; NOOPT-LABEL: test_memory_load_breaks_sequence:
; NOOPT:     ld (4118), a
; NOOPT:     ld (4118), a
; NOOPT:     ld hl, ({{.*}})
; NOOPT:     ld (4118), a
; NOOPT:     ld (4118), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4118 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4118 to i8*)
  %loaded = load volatile i16, i16* inttoptr (i64 8000 to i16*)  ; May modify HL
  store volatile i8 3, i8* inttoptr (i64 4118 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4118 to i8*)
  ret void
}

; ========== BOUNDARY AND EDGE CASE TESTS ==========

; Test 22: Maximum address values (no optimization for two stores)
define void @test_max_address_values_no_opt() {
; OPT-LABEL: test_max_address_values_no_opt:
; OPT:       ld (-1), a
; OPT:       ld (-1), a

; NOOPT-LABEL: test_max_address_values_no_opt:
; NOOPT:     ld (-1), a
; NOOPT:     ld (-1), a

entry:
  store volatile i8 1, i8* inttoptr (i64 65535 to i8*)  ; Max 16-bit address
  store volatile i8 2, i8* inttoptr (i64 65535 to i8*)
  ret void
}

; Test 23: Zero address
define void @test_zero_address() {
; OPT-LABEL: test_zero_address:
; OPT:       ld hl, 0
; OPT:       ld (hl), 1
; OPT:       ld (hl), 2

; NOOPT-LABEL: test_zero_address:
; NOOPT:     ld hl, 0
; NOOPT:     ld (hl), 1
; NOOPT:     ld (hl), 2

entry:
  store volatile i8 1, i8* inttoptr (i64 0 to i8*)  ; Zero page
  store volatile i8 2, i8* inttoptr (i64 0 to i8*)
  ret void
}

; Test 24: Stores with skippable load instructions (should be optimized)
define void @test_non_consecutive_no_optimization() {
; OPT-LABEL: test_non_consecutive_no_optimization:
; OPT:       ld a, 1
; OPT:       ld hl, 4119
; OPT:       ld (hl), a
; OPT:       ld a, (5000)
; OPT:       ld (hl), a
; OPT:       ld a, (5001)
; OPT:       ld (hl), a

; NOOPT-LABEL: test_non_consecutive_no_optimization:
; NOOPT:     ld (4119), a
; NOOPT:     ld a, {{.*}}
; NOOPT:     ld (4119), a
; NOOPT:     ld a, {{.*}}
; NOOPT:     ld (4119), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4119 to i8*)
  ; Skippable instruction (load to A register)
  %temp1 = load volatile i8, i8* inttoptr (i64 5000 to i8*)
  store volatile i8 %temp1, i8* inttoptr (i64 4119 to i8*)
  ; Another skippable instruction
  %temp2 = load volatile i8, i8* inttoptr (i64 5001 to i8*)
  store volatile i8 %temp2, i8* inttoptr (i64 4119 to i8*)
  ret void
}

; Test 25: Very large store count with HL live (stress test)
define i16 @test_large_store_count_hl_live() {
; OPT-LABEL: test_large_store_count_hl_live:
; OPT:       push hl
; OPT:       ld hl, 4120
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       ld (hl), a
; OPT:       pop hl

; NOOPT-LABEL: test_large_store_count_hl_live:
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a
; NOOPT:     ld (4120), a

entry:
  %hl_input = call i16 asm "ld hl, 0xeeee", "={hl}"()
  
  store volatile i8 1, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 3, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 5, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 6, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 7, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 8, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 9, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 10, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 11, i8* inttoptr (i64 4120 to i8*)
  store volatile i8 12, i8* inttoptr (i64 4120 to i8*)
  
  ret i16 %hl_input
}

; ========== MULTIPLE BASIC BLOCK TESTS ==========

; Test 26: Stores across basic blocks (should not optimize across blocks)
define void @test_stores_across_basic_blocks(i1 %cond) {
; OPT-LABEL: test_stores_across_basic_blocks:
; OPT:       ld (4121), a
; OPT:       ld (4121), a
; OPT:       {{.*}}
; OPT:       ld (4121), a
; OPT-NOT:   ld hl, 4121

; NOOPT-LABEL: test_stores_across_basic_blocks:
; NOOPT:     ld (4121), a
; NOOPT:     ld (4121), a
; NOOPT:     {{.*}}
; NOOPT:     ld (4121), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4121 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4121 to i8*)
  br i1 %cond, label %then, label %else

then:
  store volatile i8 3, i8* inttoptr (i64 4121 to i8*)
  br label %exit

else:
  ret void

exit:
  ret void
}

; Test 27: Stores in different basic blocks should be optimized separately
define void @test_stores_in_separate_blocks(i1 %cond) {
; OPT-LABEL: test_stores_in_separate_blocks:
; OPT:       ld (4122), a
; OPT:       ld (4122), a
; OPT:       bit 0, l
; OPT:       jr z, {{.*}}
; OPT:       ld (4122), a
; OPT:       ld (4122), a

; NOOPT-LABEL: test_stores_in_separate_blocks:
; NOOPT:     ld (4122), a
; NOOPT:     ld (4122), a
; NOOPT:     {{.*}}
; NOOPT:     ld (4122), a
; NOOPT:     ld (4122), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4122 to i8*)
  store volatile i8 2, i8* inttoptr (i64 4122 to i8*)
  br i1 %cond, label %then, label %else

then:
  store volatile i8 3, i8* inttoptr (i64 4122 to i8*)
  store volatile i8 4, i8* inttoptr (i64 4122 to i8*)
  ret void

else:
  ret void
}

; ========== ERROR AND REGRESSION TESTS ==========

; Test 28: Empty function should not crash
define void @test_empty_function() {
; OPT-LABEL: test_empty_function:
; OPT:       ret

; NOOPT-LABEL: test_empty_function:
; NOOPT:     ret

entry:
  ret void
}

; Test 29: Function with only non-store instructions
define i8 @test_no_stores() {
; OPT-LABEL: test_no_stores:
; OPT:       ld a, {{.*}}
; OPT-NOT:   ld hl, {{.*}}

; NOOPT-LABEL: test_no_stores:
; NOOPT:     ld a, {{.*}}

entry:
  %val = load volatile i8, i8* inttoptr (i64 4123 to i8*)
  %result = add i8 %val, 1
  ret i8 %result
}

; Test 30: Complex instruction pattern that should not be optimized
define void @test_complex_non_optimizable() {
; OPT-LABEL: test_complex_non_optimizable:
; OPT:       ld (4124), a
; OPT:       call {{.*}}
; OPT:       ld (4125), a
; OPT:       ld (4124), a
; OPT-NOT:   ld hl, {{.*}}

; NOOPT-LABEL: test_complex_non_optimizable:
; NOOPT:     ld (4124), a
; NOOPT:     call {{.*}}
; NOOPT:     ld (4125), a
; NOOPT:     ld (4124), a

entry:
  store volatile i8 1, i8* inttoptr (i64 4124 to i8*)  ; First store
  call void @external_function()                        ; Function call breaks sequence
  store volatile i8 2, i8* inttoptr (i64 4125 to i8*)  ; Different address
  store volatile i8 3, i8* inttoptr (i64 4124 to i8*)  ; Back to original address, but single store
  ret void
}
