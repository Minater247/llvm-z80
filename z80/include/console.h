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

        static void print_at(uint8_t x, uint8_t y, const char *str) __attribute__((noinline)) {
            putchar(22);
            putchar(y);
            putchar(x);
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

        static void set_print_attr(uint8_t color) {
            uint8_t *attr_t = (uint8_t*)0x5C8F; // ATTR_T @ 0x5C8F
            *attr_t = color;
        }
    };
} // namespace ZX

