; Optimized IR snapshot of the matching ordinary-C benchmark source.
source_filename = "pcm-state.c"
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(readwrite, inaccessiblemem: none, target_mem0: none, target_mem1: none)
define dso_local void @pcm_advance(ptr noundef captures(none) %0) local_unnamed_addr #0 {
  %2 = load ptr, ptr %0, align 1, !tbaa !7
  %3 = getelementptr inbounds nuw i8, ptr %0, i16 2
  %4 = load ptr, ptr %3, align 1, !tbaa !11
  %5 = getelementptr inbounds nuw i8, ptr %0, i16 4
  %6 = load ptr, ptr %5, align 1, !tbaa !12
  %7 = ptrtoint ptr %6 to i16
  %8 = and i16 %7, -256
  %9 = getelementptr inbounds nuw i8, ptr %4, i16 1
  %10 = load i8, ptr %4, align 1, !tbaa !13
  %11 = zext i8 %10 to i16
  %12 = or disjoint i16 %8, %11
  %13 = inttoptr i16 %12 to ptr
  %14 = load i8, ptr %13, align 1, !tbaa !13
  %15 = load i8, ptr %2, align 1, !tbaa !13
  %16 = tail call i8 @llvm.sadd.sat.i8(i8 %15, i8 %14)
  store i8 %16, ptr %2, align 1, !tbaa !13
  %17 = ptrtoint ptr %2 to i16
  %18 = and i16 %17, -256
  %19 = add i16 %17, 1
  %20 = and i16 %19, 255
  %21 = or disjoint i16 %20, %18
  %22 = inttoptr i16 %21 to ptr
  %23 = getelementptr inbounds nuw i8, ptr %4, i16 2
  %24 = load i8, ptr %9, align 1, !tbaa !13
  %25 = zext i8 %24 to i16
  %26 = or disjoint i16 %8, %25
  %27 = inttoptr i16 %26 to ptr
  %28 = load i8, ptr %27, align 1, !tbaa !13
  %29 = load i8, ptr %22, align 1, !tbaa !13
  %30 = tail call i8 @llvm.sadd.sat.i8(i8 %29, i8 %28)
  store i8 %30, ptr %22, align 1, !tbaa !13
  %31 = add i16 %17, 2
  %32 = and i16 %31, 255
  %33 = or disjoint i16 %32, %18
  %34 = inttoptr i16 %33 to ptr
  store ptr %34, ptr %0, align 1, !tbaa !7
  store ptr %23, ptr %3, align 1, !tbaa !11
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
!7 = !{!8, !9, i64 0}
!8 = !{!"pcm_state", !9, i64 0, !9, i64 2, !9, i64 4, !9, i64 6}
!9 = !{!"p1 omnipotent char", !10, i64 0}
!10 = !{!"any pointer", !5, i64 0}
!11 = !{!8, !9, i64 2}
!12 = !{!8, !9, i64 4}
!13 = !{!5, !5, i64 0}
