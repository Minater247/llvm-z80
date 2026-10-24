// RUN: %clang --target=z80 -mexpnoints -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=ez80 -mexpnoints -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=z80 -mexpnoints -mno-expnoints -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang --target=z80 -mno-expnoints -mexpnoints -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=z80 -### -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// ON: "-target-feature" "+expnoints"
// OFF: "-target-feature" "-expnoints"
// DEFAULT-NOT: "-target-feature" "+expnoints"
// DEFAULT-NOT: "-target-feature" "-expnoints"
int f(int x) { return x + 1; }
