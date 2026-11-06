; RUN: llc -stop-before=irtranslator %s -o - | FileCheck %s
; Private read-only bytes can be shared after target-specific specialization.
target triple = "z80"
@private = internal global i8 0
@escaped = internal global i8 0
@escape = global ptr @escaped
@volatile = internal global i8 0

declare i8 @llvm.umin.i8(i8, i8)

define void @set(i8 %value) {
  store i8 %value, ptr @private
  ret void
}

define i8 @read() {
; CHECK-LABEL: define i8 @read()
; CHECK: %private.value = load i8, ptr @private
; CHECK-NOT: load i8, ptr @private
; CHECK: ret i8 %private.value
  %first = load i8, ptr @private
  store volatile i8 %first, ptr inttoptr (i16 8192 to ptr)
  %second = load i8, ptr @private
  store volatile i8 %second, ptr inttoptr (i16 8193 to ptr)
  ret i8 %second
}

define i8 @intrinsic() {
; CHECK-LABEL: define i8 @intrinsic()
; CHECK: %private.value = load i8, ptr @private
; CHECK-NOT: load i8, ptr @private
; CHECK: call i8 @llvm.umin.i8(i8 %private.value, i8 120)
  %first = load i8, ptr @private
  store volatile i8 %first, ptr inttoptr (i16 8192 to ptr)
  %second = load i8, ptr @private
  %bound = call i8 @llvm.umin.i8(i8 %second, i8 120)
  ret i8 %bound
}

define i8 @write(i8 %value) {
; CHECK-LABEL: define i8 @write(
; CHECK-NOT: private.value
; CHECK: load i8, ptr @private
; CHECK: store i8 %value, ptr @private
; CHECK: load i8, ptr @private
  %first = load i8, ptr @private
  store volatile i8 %first, ptr inttoptr (i16 8192 to ptr)
  store i8 %value, ptr @private
  %second = load i8, ptr @private
  ret i8 %second
}

define i8 @barrier() {
; CHECK-LABEL: define i8 @barrier()
; CHECK-NOT: private.value
; CHECK: load i8, ptr @private
; CHECK: asm sideeffect
; CHECK: load i8, ptr @private
  %first = load i8, ptr @private
  store volatile i8 %first, ptr inttoptr (i16 8192 to ptr)
  call void asm sideeffect "", "~{memory}"()
  %second = load i8, ptr @private
  ret i8 %second
}

define i8 @escaping() {
; CHECK-LABEL: define i8 @escaping()
; CHECK-NOT: private.value
; CHECK-COUNT-2: load i8, ptr @escaped
  %first = load i8, ptr @escaped
  store volatile i8 %first, ptr inttoptr (i16 8192 to ptr)
  %second = load i8, ptr @escaped
  ret i8 %second
}

define i8 @observable() {
; CHECK-LABEL: define i8 @observable()
; CHECK-NOT: private.value
; CHECK-COUNT-2: load volatile i8, ptr @volatile
  %first = load volatile i8, ptr @volatile
  store volatile i8 %first, ptr inttoptr (i16 8192 to ptr)
  %second = load volatile i8, ptr @volatile
  ret i8 %second
}
