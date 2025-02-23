#pragma once

namespace ZX {
    using int8_t = char;
    using uint8_t = unsigned char;
    using int16_t = int;
    using uint16_t = unsigned int;

    struct Console {
        static void putchar(char c) {
            uint16_t iy = 23610;
            __asm__ ("rst $10" : "=a"(c), "=iy"(iy) : "a"(c), "iy"(iy) : "h", "l", "d", "e", "b", "c", "cc", "memory");
        }

        static void at(uint8_t x, uint8_t y) {
            putchar(22);
            putchar(y);
            putchar(x);
        }

        static void print(const char *str) {
            while (*str)
                putchar(*str++);
        }
    };
};

