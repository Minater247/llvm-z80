; Register allocation for ring-buffer and table-lookup loops.
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/regalloc-sound.py %t.s --max-ring-cycles 142 --max-mix-cycles 634
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/regalloc-sound.py %t.bank.s --max-ring-cycles 131 --max-mix-cycles 438
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/regalloc-sound.py %t.greedy.s

target triple = "z80"

define dso_local void @mix_four(ptr noundef writeonly captures(none) %0, ptr noundef readonly captures(none) %1, ptr noundef readonly captures(none) %2, ptr noundef readonly captures(none) %3, ptr noundef readonly captures(none) %4, ptr noundef readonly captures(none) %5, i8 noundef zeroext %6) local_unnamed_addr #3 {
  %8 = icmp eq i8 %6, 0
  br i1 %8, label %42, label %9

9:                                                ; preds = %7, %9
  %10 = phi ptr [ %40, %9 ], [ %0, %7 ]
  %11 = phi ptr [ %17, %9 ], [ %1, %7 ]
  %12 = phi ptr [ %22, %9 ], [ %2, %7 ]
  %13 = phi ptr [ %27, %9 ], [ %3, %7 ]
  %14 = phi i8 [ %16, %9 ], [ %6, %7 ]
  %15 = phi ptr [ %32, %9 ], [ %4, %7 ]
  %16 = add i8 %14, -1
  %17 = getelementptr inbounds nuw i8, ptr %11, i16 1
  %18 = load i8, ptr %11, align 1, !tbaa !9
  %19 = zext i8 %18 to i16
  %20 = getelementptr inbounds nuw i8, ptr %5, i16 %19
  %21 = load i8, ptr %20, align 1, !tbaa !9
  %22 = getelementptr inbounds nuw i8, ptr %12, i16 1
  %23 = load i8, ptr %12, align 1, !tbaa !9
  %24 = zext i8 %23 to i16
  %25 = getelementptr inbounds nuw i8, ptr %5, i16 %24
  %26 = load i8, ptr %25, align 1, !tbaa !9
  %27 = getelementptr inbounds nuw i8, ptr %13, i16 1
  %28 = load i8, ptr %13, align 1, !tbaa !9
  %29 = zext i8 %28 to i16
  %30 = getelementptr inbounds nuw i8, ptr %5, i16 %29
  %31 = load i8, ptr %30, align 1, !tbaa !9
  %32 = getelementptr inbounds nuw i8, ptr %15, i16 1
  %33 = load i8, ptr %15, align 1, !tbaa !9
  %34 = zext i8 %33 to i16
  %35 = getelementptr inbounds nuw i8, ptr %5, i16 %34
  %36 = load i8, ptr %35, align 1, !tbaa !9
  %37 = add i8 %26, %21
  %38 = add i8 %37, %31
  %39 = add i8 %38, %36
  %40 = getelementptr inbounds nuw i8, ptr %10, i16 1
  store i8 %39, ptr %10, align 1, !tbaa !9
  %41 = icmp eq i8 %16, 0
  br i1 %41, label %42, label %9, !llvm.loop !10

42:                                               ; preds = %9, %7
  ret void
}

define dso_local void @mix_ring(ptr noundef captures(none) %0, ptr noundef readonly captures(none) %1, i8 noundef zeroext %2, i8 noundef zeroext %3) local_unnamed_addr #3 {
  %5 = icmp eq i8 %3, 0
  br i1 %5, label %19, label %6

6:                                                ; preds = %4, %6
  %7 = phi i8 [ %10, %6 ], [ %3, %4 ]
  %8 = phi i8 [ %17, %6 ], [ %2, %4 ]
  %9 = phi ptr [ %11, %6 ], [ %1, %4 ]
  %10 = add i8 %7, -1
  %11 = getelementptr inbounds nuw i8, ptr %9, i16 1
  %12 = load i8, ptr %9, align 1, !tbaa !9
  %13 = zext i8 %8 to i16
  %14 = getelementptr inbounds nuw i8, ptr %0, i16 %13
  %15 = load i8, ptr %14, align 1, !tbaa !9
  %16 = add i8 %15, %12
  store i8 %16, ptr %14, align 1, !tbaa !9
  %17 = add i8 %8, 1
  %18 = icmp eq i8 %10, 0
  br i1 %18, label %19, label %6, !llvm.loop !13

19:                                               ; preds = %6, %4
  ret void
}

attributes #3 = { nofree norecurse nosync nounwind memory(argmem: readwrite) "frame-pointer"="all" }
!3 = !{!4, !4, i64 0}
!4 = !{!"int", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C/C++ TBAA"}
!7 = !{!8, !8, i64 0}
!8 = !{!"short", !5, i64 0}
!9 = !{!5, !5, i64 0}
!10 = distinct !{!10, !11}
!11 = !{!"llvm.loop.mustprogress"}
!12 = distinct !{!12, !11}
!13 = distinct !{!13, !11}
