#pragma once

namespace ZX {
    struct Console {
        static void putchar(char c) {
            uint16_t iy = 23610;
            __asm__ ("rst $10" : "=a"(c) : "a"(c), "iy"(iy) : "h", "l", "d", "e", "b", "c", "cc", "memory");
        }

        static void at(uint8_t x, uint8_t y) __attribute__((noinline)) {
            putchar(22);
            putchar(y);
            putchar(x);
        }

        static void print(const char *str) __attribute__((noinline)) {
            while (*str)
                putchar(*str++);
        }

        static void border(uint8_t color) {
            __asm__ volatile (
                "out\t(0xfe), a"
                :
                : "a"(color)
                :
            );
        }
    };
} // namespace ZX

