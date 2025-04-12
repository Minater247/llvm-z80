#pragma once

namespace ZX {
    struct ROM {
        static const uint8_t *character_data(char c) {
            return (const uint8_t*)0x3D00 + (c - 32) * 8;
        }
    };
} // namespace ZX

