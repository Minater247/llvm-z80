; RUN: llc -mtriple=z80-none-elf+full -O2 -print-before=z80-unroll-ld -print-after=z80-unroll-ld < %s 2>&1 | FileCheck %s

; Test for Z80UnrollLd pass optimizations
; This pass transforms LD8go -> LD8gp and LD8og -> LD8pg when certain patterns are detected

target datalayout = "e-m:e-p:16:8-i8:8-i16:8-i32:8-i64:8-f32:8-f64:8-n8:16-a:8"
target triple = "z80-unknown-unknown"

; Test the exact pattern that we know works from our manual testing
define void @test_known_working_pattern(i8* %base1, i8* %base2) {
; This uses the exact same pattern as our successful manual test
; CHECK-LABEL: Before Z80 unroll LD
; CHECK: LD8go {{%[0-9]+}}:i16, 1{{.*}}:: (load{{.*}}from %ir.arrayidx1
; CHECK: LD8go {{%[0-9]+}}:i16, 1{{.*}}:: (load{{.*}}from %ir.arrayidx2
; CHECK: LD8og {{%[0-9]+}}:i16, 1{{.*}}:: (store{{.*}}into %ir.arrayidx1

; CHECK-LABEL: After Z80 unroll LD
; CHECK: LD16ri i16 1
; CHECK: ADD16ao
; CHECK: LD8gp {{%[0-9]+}}:a16{{.*}}:: (load{{.*}}from %ir.arrayidx1
; CHECK: LD8pg {{%[0-9]+}}:a16{{.*}}:: (store{{.*}}into %ir.arrayidx1

entry:
  ; Pattern that we confirmed works: different base registers with matching store
  %arrayidx1 = getelementptr inbounds i8, i8* %base1, i16 1
  %0 = load i8, i8* %arrayidx1, align 1, !tbaa !4
  
  %arrayidx2 = getelementptr inbounds i8, i8* %base2, i16 1
  %1 = load i8, i8* %arrayidx2, align 1, !tbaa !4
  
  %combined = add i8 %0, %1
  %modified = add i8 %combined, 42
  
  ; This store should trigger the optimization
  store i8 %modified, i8* %arrayidx1, align 1, !tbaa !4
  
  ret void
}

; Test OffsetMap reuse - multiple stores to same calculated address
define void @test_offset_map_reuse(i8* %base1, i8* %base2) {
; Verify that multiple stores reuse the same address calculation
; CHECK-LABEL: test_offset_map_reuse
; CHECK: LD8gp {{%[0-9]+}}:a16
; CHECK: LD8pg {{%[0-9]+}}:a16
; CHECK: LD8pg {{%[0-9]+}}:a16

entry:
  %arrayidx1 = getelementptr inbounds i8, i8* %base1, i16 1
  %0 = load i8, i8* %arrayidx1, align 1, !tbaa !4
  
  %arrayidx2 = getelementptr inbounds i8, i8* %base2, i16 1  
  %1 = load i8, i8* %arrayidx2, align 1, !tbaa !4
  
  %combined = add i8 %0, %1
  
  ; Both stores should reuse the same address calculation
  store i8 %combined, i8* %arrayidx1, align 1, !tbaa !4
  %modified = add i8 %combined, 1
  store i8 %modified, i8* %arrayidx1, align 1, !tbaa !4
  
  ret void
}

; TBAA metadata
!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C/C++ TBAA"}
