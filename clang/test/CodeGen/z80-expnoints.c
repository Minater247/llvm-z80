// RUN: %clang_cc1 -triple z80 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple ez80 -emit-llvm -o - %s | FileCheck %s
// CHECK: define{{.*}} @promised{{.*}} #[[ON:[0-9]+]]
__attribute__((expnoints)) void promised(void) {}
// CHECK: define{{.*}} @ordinary{{.*}} #[[OFF:[0-9]+]]
void ordinary(void) {}
// CHECK-DAG: attributes #[[ON]] = { {{.*}}"z80-expnoints"{{.*}} }
// CHECK-DAG: attributes #[[OFF]] = { {{[^}]*}} }
// CHECK-NOT: attributes #[[OFF]] = { {{.*}}"z80-expnoints"
