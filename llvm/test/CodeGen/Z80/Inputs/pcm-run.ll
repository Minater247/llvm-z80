; Optimized IR snapshot of the matching ordinary-C benchmark source.
source_filename = "pcm-run.c"
target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"

; Function Attrs: nounwind
define dso_local zeroext range(i8 0, 3) i8 @pcm_run(ptr noundef %0) local_unnamed_addr #0 {
  %2 = getelementptr inbounds nuw i8, ptr %0, i16 2
  %3 = load ptr, ptr %2, align 1, !tbaa !7
  %4 = getelementptr inbounds nuw i8, ptr %0, i16 6
  %5 = load ptr, ptr %4, align 1, !tbaa !11
  %6 = icmp eq ptr %3, %5
  br i1 %6, label %12, label %7

7:                                                ; preds = %1
  tail call void @pcm_advance(ptr noundef nonnull %0) #2
  %8 = load ptr, ptr %2, align 1, !tbaa !7
  %9 = load ptr, ptr %4, align 1, !tbaa !11
  %10 = icmp eq ptr %8, %9
  br i1 %10, label %12, label %11

11:                                               ; preds = %7
  tail call void @pcm_advance(ptr noundef nonnull %0) #2
  br label %12

12:                                               ; preds = %7, %1, %11
  %13 = phi i8 [ 2, %11 ], [ 0, %1 ], [ 1, %7 ]
  ret i8 %13
}

declare dso_local void @pcm_advance(ptr noundef) local_unnamed_addr #1

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
!7 = !{!8, !9, i64 2}
!8 = !{!"pcm_state", !9, i64 0, !9, i64 2, !9, i64 4, !9, i64 6}
!9 = !{!"p1 omnipotent char", !10, i64 0}
!10 = !{!"any pointer", !5, i64 0}
!11 = !{!8, !9, i64 6}
