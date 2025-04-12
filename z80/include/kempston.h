#pragma once

namespace ZX {
    struct Kempston {
        static uint8_t read() {
            uint8_t a;
            // Kempston Joystick is at port 0x001f
            __asm__ volatile (
                "in\ta, (0x1f)"
                : "=a"(a)
                : "a"((uint8_t)0)
                :
            );
            return a;
        }
    };
} // namespace ZX

