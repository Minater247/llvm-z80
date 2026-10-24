// RUN: %clang --target=z80 -malternate-registers -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=ez80 -malternate-registers -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=z80 -malternate-registers -mno-alternate-registers -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang --target=z80 -mno-alternate-registers -malternate-registers -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=z80 -### -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// ON: "-target-feature" "+alternate-regs"
// ON-NOT: "-alternate-regs"
// OFF: "-target-feature" "-alternate-regs"
// OFF-NOT: "+alternate-regs"
// DEFAULT-NOT: alternate-regs
int f(int x) { return x + 1; }
