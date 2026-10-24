; Optimized IR snapshot of the matching ordinary-C benchmark source.
source_filename = "pcm-advance.c"
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"

; Function Attrs: nounwind
define dso_local void @pcm_advance(ptr noundef captures(none) %0) local_unnamed_addr #0 {
  %2 = load ptr, ptr %0, align 1, !tbaa !7
  %3 = getelementptr inbounds nuw i8, ptr %0, i16 2
  %4 = load ptr, ptr %3, align 1, !tbaa !11
  %5 = getelementptr inbounds nuw i8, ptr %0, i16 4
  %6 = load ptr, ptr %5, align 1, !tbaa !12
  tail call void @pcm_mix2(ptr noundef %2, ptr noundef %4, ptr noundef %6) #2
  %7 = load ptr, ptr %0, align 1, !tbaa !7
  %8 = ptrtoint ptr %7 to i16
  %9 = and i16 %8, -256
  %10 = add i16 %8, 2
  %11 = and i16 %10, 255
  %12 = or disjoint i16 %11, %9
  %13 = inttoptr i16 %12 to ptr
  store ptr %13, ptr %0, align 1, !tbaa !7
  %14 = load ptr, ptr %3, align 1, !tbaa !11
  %15 = getelementptr inbounds nuw i8, ptr %14, i16 2
  store ptr %15, ptr %3, align 1, !tbaa !11
  ret void
}

declare dso_local void @pcm_mix2(ptr noundef, ptr noundef, ptr noundef) local_unnamed_addr #1

attributes #0 = { nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nounwind }

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
