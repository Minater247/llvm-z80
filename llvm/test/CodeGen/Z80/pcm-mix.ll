; Saturating byte lookups with wrapping destination offsets.
; No source annotations or persistent register state. Check results, page wrap,
; ABI preservation, ROM reads and a 122-T-state ceiling after input setup and
; before outer exit shuffling.
; RUN: llc -verify-machineinstrs %s -o %t.s
; RUN: %python %S/Inputs/pcm-check.py %t.s --max-cycles 122
; RUN: llc -regalloc=greedy -verify-machineinstrs %s -o %t.greedy.s
; RUN: %python %S/Inputs/pcm-check.py %t.greedy.s --max-cycles 122
; RUN: llc -mattr=+alternate-regs -verify-machineinstrs %s -o %t.bank.s
; RUN: %python %S/Inputs/pcm-check.py %t.bank.s --max-cycles 122 --alternate-registers
; RUN: llc -mattr=+alternate-regs -regalloc=greedy -verify-machineinstrs %s -o %t.greedy-bank.s
; RUN: %python %S/Inputs/pcm-check.py %t.greedy-bank.s --max-cycles 122 --alternate-registers

; With sequential source reads, expnoints cannot justify prefetch across stores.
; RUN: llc -mattr=+expnoints -verify-machineinstrs %s -o %t.no-prefetch.s
; RUN: FileCheck %s --check-prefix=NO-PREFETCH < %t.no-prefetch.s
; RUN: %python %S/Inputs/pcm-check.py %t.no-prefetch.s --max-cycles 122
; NO-PREFETCH-LABEL: pcm_mix2:
; NO-PREFETCH-NOT: pop bc
; NO-PREFETCH-NOT: ld sp,
; NO-PREFETCH: ret

target datalayout = "e-m:e-p:16:8-p2:8:8-p3:16:8-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16-S8"
target triple = "z80"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(readwrite, inaccessiblemem: none, target_mem0: none, target_mem1: none)
define dso_local void @pcm_mix2(ptr noundef %0, ptr noundef readonly captures(none) %1, ptr noundef %2) local_unnamed_addr {
  %4 = ptrtoint ptr %0 to i16
  %5 = and i16 %4, -256
  %6 = ptrtoint ptr %2 to i16
  %7 = and i16 %6, -256
  %8 = load i8, ptr %1, align 1, !tbaa !7
  %9 = zext i8 %8 to i16
  %10 = or disjoint i16 %7, %9
  %11 = inttoptr i16 %10 to ptr
  %12 = load i8, ptr %11, align 1, !tbaa !7
  %13 = load i8, ptr %0, align 1, !tbaa !7
  %14 = tail call i8 @llvm.sadd.sat.i8(i8 %13, i8 %12)
  store i8 %14, ptr %0, align 1, !tbaa !7
  %15 = add i16 %4, 1
  %16 = and i16 %15, 255
  %17 = or disjoint i16 %5, %16
  %18 = inttoptr i16 %17 to ptr
  %19 = getelementptr inbounds nuw i8, ptr %1, i16 1
  %20 = load i8, ptr %19, align 1, !tbaa !7
  %21 = zext i8 %20 to i16
  %22 = or disjoint i16 %7, %21
  %23 = inttoptr i16 %22 to ptr
  %24 = load i8, ptr %23, align 1, !tbaa !7
  %25 = load i8, ptr %18, align 1, !tbaa !7
  %26 = tail call i8 @llvm.sadd.sat.i8(i8 %25, i8 %24)
  store i8 %26, ptr %18, align 1, !tbaa !7
  ret void
}

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i8 @llvm.sadd.sat.i8(i8, i8)

!7 = !{!5, !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C/C++ TBAA"}
