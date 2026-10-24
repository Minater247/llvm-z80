// RUN: %clang_cc1 -triple z80 -fsyntax-only -verify %s
__attribute__((expnoints)) void f(void);
__attribute__((expnoints(1))) void g(void); // expected-error {{'expnoints' attribute takes no arguments}}
__attribute__((expnoints)) int x; // expected-warning {{'expnoints' attribute only applies to functions}}
