; Paired input loads followed by saturating byte lookups.
; RUN: llc -mattr=+expnoints -verify-machineinstrs %s -o %t.s
; RUN: FileCheck %s < %t.s
; RUN: %python %S/Inputs/pcm-check.py %t.s --max-cycles 112
; RUN: llc -mattr=+expnoints -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/pcm-check.py %t.greedy.s --max-cycles 112
; RUN: llc -verify-machineinstrs %s -o - | FileCheck %s --check-prefix=RESERVED
; CHECK-LABEL: pcm_mix2:
; CHECK: ld sp, hl
; CHECK: pop bc
; CHECK: ld sp, iy
; CHECK: ret
; RESERVED-LABEL: pcm_mix2:
; RESERVED-NOT: ld sp,
; RESERVED-NOT: pop bc
; RESERVED: ret
source_filename = "pcm-pair.c"
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(readwrite, inaccessiblemem: none, target_mem0: none, target_mem1: none)
define dso_local void @pcm_mix2(ptr noundef %0, ptr noundef readonly captures(none) %1, ptr noundef %2) local_unnamed_addr #0 {
  %4 = load i8, ptr %1, align 1, !tbaa !7
  %5 = getelementptr inbounds nuw i8, ptr %1, i16 1
  %6 = load i8, ptr %5, align 1, !tbaa !7
  %7 = ptrtoint ptr %0 to i16
  %8 = and i16 %7, -256
  %9 = ptrtoint ptr %2 to i16
  %10 = and i16 %9, -256
  %11 = zext i8 %4 to i16
  %12 = or disjoint i16 %10, %11
  %13 = inttoptr i16 %12 to ptr
  %14 = load i8, ptr %13, align 1, !tbaa !7
  %15 = load i8, ptr %0, align 1, !tbaa !7
  %16 = tail call i8 @llvm.sadd.sat.i8(i8 %15, i8 %14)
  store i8 %16, ptr %0, align 1, !tbaa !7
  %17 = add i16 %7, 1
  %18 = and i16 %17, 255
  %19 = or disjoint i16 %8, %18
  %20 = inttoptr i16 %19 to ptr
  %21 = zext i8 %6 to i16
  %22 = or disjoint i16 %10, %21
  %23 = inttoptr i16 %22 to ptr
  %24 = load i8, ptr %23, align 1, !tbaa !7
  %25 = load i8, ptr %20, align 1, !tbaa !7
  %26 = tail call i8 @llvm.sadd.sat.i8(i8 %25, i8 %24)
  store i8 %26, ptr %20, align 1, !tbaa !7
  ret void
}

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i8 @llvm.sadd.sat.i8(i8, i8) #1

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(readwrite, inaccessiblemem: none, target_mem0: none, target_mem1: none) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}
!llvm.errno.tbaa = !{!3}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{!"clang version 22.0.0git (https://github.com/Minater247/llvm-z80 0b8ddb99f8509e34f93a655049e9db7a635a7737)"}
!3 = !{!4, !4, i64 0}
!4 = !{!"int", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C/C++ TBAA"}
!7 = !{!5, !5, i64 0}
