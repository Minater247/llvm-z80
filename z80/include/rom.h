#pragma once

namespace ZX {
    struct ROM {
        enum : uint16_t {
            CHARACTER_DATA = 0x3D00,
        };
        static const uint8_t *character_data(char c) {
            return (const uint8_t*)CHARACTER_DATA + (c - 32) * 8;
        }
    };
} // namespace ZX

