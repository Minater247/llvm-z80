#pragma once

namespace std {
    using int8_t = char;
    using uint8_t = unsigned char;
    using int16_t = int;
    using uint16_t = unsigned int;
    using size_t = uint16_t;
    using int32_t = long;
    using uint32_t = unsigned long;
};

using int8_t = std::int8_t;
using uint8_t = std::uint8_t;
using int16_t = std::int16_t;
using uint16_t = std::uint16_t;
using int32_t = std::int32_t;
using uint32_t = std::uint32_t;

extern "C" {
    void *memset(void *s, uint8_t c, uint16_t n);
    void *memcpy(void *dest, const void *src, uint16_t n);
};

#define BIN16(b) { \
    static_cast<uint8_t>((((uint32_t)b) >> 8) & 0xFF), \
    static_cast<uint8_t>(((uint32_t)b) & 0xFF) \
}

#define BIN24(b) { \
    static_cast<uint8_t>(((uint32_t)b) >> 16), \
    static_cast<uint8_t>((((uint32_t)b) >> 8) & 0xFF), \
    static_cast<uint8_t>(((uint32_t)b) & 0xFF) \
}

#define BIN32(b) { \
    static_cast<uint8_t>(((uint32_t)b) >> 24), \
    static_cast<uint8_t>(((uint32_t)b) >> 16), \
    static_cast<uint8_t>((((uint32_t)b) >> 8) & 0xFF), \
    static_cast<uint8_t>(((uint32_t)b) & 0xFF) \
}

namespace ZX {
    inline void disable_interrupts() { __asm__ volatile("di"); }
    inline void enable_interrupts() { __asm__ volatile("ei"); }

    struct ScopedDisableInterrupts {
        ScopedDisableInterrupts() {
            disable_interrupts();
        }

        ScopedDisableInterrupts(ScopedDisableInterrupts&) = delete;
        ScopedDisableInterrupts& operator=(ScopedDisableInterrupts&) = delete;

        ~ScopedDisableInterrupts() {
            enable_interrupts();
        }
    };

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
    };

    namespace Keyboard {
       template <uint8_t PORT>
        static uint8_t _read_row() {
            uint8_t a;
            __asm__ volatile (
                "in\ta, (0xfe)"
                : "=a"(a)
                : "a"(PORT)
                :
            );
            return a;
        }

        template <char C, uint8_t PORT, uint8_t BIT, bool SHIFT = false> struct Key {
            constexpr char get_character() const { return C; }
            constexpr uint8_t get_port() const { return PORT; }
            constexpr uint8_t get_mask() const { return 1 << BIT; }
            constexpr bool shifted() const { return SHIFT; }

            bool is_pressed() const {
                if (SHIFT) {
                    if (_read_row<0xfe>() & 1)
                        return false;
                }
                return !(_read_row<PORT>() & get_mask());
            }
        };

        static Key<'m', 0x7f, 2> KEY_M;
        static Key<'b', 0x7f, 4> KEY_B;
    };

    template <uint16_t ADDRESS>
    struct _Screen {
        // video memory is divided into 3 sections of 64 rows
        // each section has 8 interleaved scanlines
        static uint8_t * ptr() { return (uint8_t*)ADDRESS; }

        constexpr static uint16_t WIDTH = 256;
        constexpr static uint8_t HEIGHT = 192;
        constexpr static uint8_t BANK_ROWS = 64;
        constexpr static uint8_t ROW_SIZE = WIDTH / 8;
        constexpr static uint16_t BANK_SIZE = BANK_ROWS * (uint16_t)ROW_SIZE;
        constexpr static uint16_t SCANLINE_SIZE = BANK_SIZE / 8;

        // row addresses lookup table.
        constexpr static uint16_t row_addresses[192] = {ADDRESS + 0x0000, ADDRESS + 0x0100, ADDRESS + 0x0200, ADDRESS + 0x0300, ADDRESS + 0x0400, ADDRESS + 0x0500, ADDRESS + 0x0600, ADDRESS + 0x0700, ADDRESS + 0x0020, ADDRESS + 0x0120, ADDRESS + 0x0220, ADDRESS + 0x0320, ADDRESS + 0x0420, ADDRESS + 0x0520, ADDRESS + 0x0620, ADDRESS + 0x0720, ADDRESS + 0x0040, ADDRESS + 0x0140, ADDRESS + 0x0240, ADDRESS + 0x0340, ADDRESS + 0x0440, ADDRESS + 0x0540, ADDRESS + 0x0640, ADDRESS + 0x0740, ADDRESS + 0x0060, ADDRESS + 0x0160, ADDRESS + 0x0260, ADDRESS + 0x0360, ADDRESS + 0x0460, ADDRESS + 0x0560, ADDRESS + 0x0660, ADDRESS + 0x0760, ADDRESS + 0x0080, ADDRESS + 0x0180, ADDRESS + 0x0280, ADDRESS + 0x0380, ADDRESS + 0x0480, ADDRESS + 0x0580, ADDRESS + 0x0680, ADDRESS + 0x0780, ADDRESS + 0x00a0, ADDRESS + 0x01a0, ADDRESS + 0x02a0, ADDRESS + 0x03a0, ADDRESS + 0x04a0, ADDRESS + 0x05a0, ADDRESS + 0x06a0, ADDRESS + 0x07a0, ADDRESS + 0x00c0, ADDRESS + 0x01c0, ADDRESS + 0x02c0, ADDRESS + 0x03c0, ADDRESS + 0x04c0, ADDRESS + 0x05c0, ADDRESS + 0x06c0, ADDRESS + 0x07c0, ADDRESS + 0x00e0, ADDRESS + 0x01e0, ADDRESS + 0x02e0, ADDRESS + 0x03e0, ADDRESS + 0x04e0, ADDRESS + 0x05e0, ADDRESS + 0x06e0, ADDRESS + 0x07e0, ADDRESS + 0x0800, ADDRESS + 0x0900, ADDRESS + 0x0a00, ADDRESS + 0x0b00, ADDRESS + 0x0c00, ADDRESS + 0x0d00, ADDRESS + 0x0e00, ADDRESS + 0x0f00, ADDRESS + 0x0820, ADDRESS + 0x0920, ADDRESS + 0x0a20, ADDRESS + 0x0b20, ADDRESS + 0x0c20, ADDRESS + 0x0d20, ADDRESS + 0x0e20, ADDRESS + 0x0f20, ADDRESS + 0x0840, ADDRESS + 0x0940, ADDRESS + 0x0a40, ADDRESS + 0x0b40, ADDRESS + 0x0c40, ADDRESS + 0x0d40, ADDRESS + 0x0e40, ADDRESS + 0x0f40, ADDRESS + 0x0860, ADDRESS + 0x0960, ADDRESS + 0x0a60, ADDRESS + 0x0b60, ADDRESS + 0x0c60, ADDRESS + 0x0d60, ADDRESS + 0x0e60, ADDRESS + 0x0f60, ADDRESS + 0x0880, ADDRESS + 0x0980, ADDRESS + 0x0a80, ADDRESS + 0x0b80, ADDRESS + 0x0c80, ADDRESS + 0x0d80, ADDRESS + 0x0e80, ADDRESS + 0x0f80, ADDRESS + 0x08a0, ADDRESS + 0x09a0, ADDRESS + 0x0aa0, ADDRESS + 0x0ba0, ADDRESS + 0x0ca0, ADDRESS + 0x0da0, ADDRESS + 0x0ea0, ADDRESS + 0x0fa0, ADDRESS + 0x08c0, ADDRESS + 0x09c0, ADDRESS + 0x0ac0, ADDRESS + 0x0bc0, ADDRESS + 0x0cc0, ADDRESS + 0x0dc0, ADDRESS + 0x0ec0, ADDRESS + 0x0fc0, ADDRESS + 0x08e0, ADDRESS + 0x09e0, ADDRESS + 0x0ae0, ADDRESS + 0x0be0, ADDRESS + 0x0ce0, ADDRESS + 0x0de0, ADDRESS + 0x0ee0, ADDRESS + 0x0fe0, ADDRESS + 0x1000, ADDRESS + 0x1100, ADDRESS + 0x1200, ADDRESS + 0x1300, ADDRESS + 0x1400, ADDRESS + 0x1500, ADDRESS + 0x1600, ADDRESS + 0x1700, ADDRESS + 0x1020, ADDRESS + 0x1120, ADDRESS + 0x1220, ADDRESS + 0x1320, ADDRESS + 0x1420, ADDRESS + 0x1520, ADDRESS + 0x1620, ADDRESS + 0x1720, ADDRESS + 0x1040, ADDRESS + 0x1140, ADDRESS + 0x1240, ADDRESS + 0x1340, ADDRESS + 0x1440, ADDRESS + 0x1540, ADDRESS + 0x1640, ADDRESS + 0x1740, ADDRESS + 0x1060, ADDRESS + 0x1160, ADDRESS + 0x1260, ADDRESS + 0x1360, ADDRESS + 0x1460, ADDRESS + 0x1560, ADDRESS + 0x1660, ADDRESS + 0x1760, ADDRESS + 0x1080, ADDRESS + 0x1180, ADDRESS + 0x1280, ADDRESS + 0x1380, ADDRESS + 0x1480, ADDRESS + 0x1580, ADDRESS + 0x1680, ADDRESS + 0x1780, ADDRESS + 0x10a0, ADDRESS + 0x11a0, ADDRESS + 0x12a0, ADDRESS + 0x13a0, ADDRESS + 0x14a0, ADDRESS + 0x15a0, ADDRESS + 0x16a0, ADDRESS + 0x17a0, ADDRESS + 0x10c0, ADDRESS + 0x11c0, ADDRESS + 0x12c0, ADDRESS + 0x13c0, ADDRESS + 0x14c0, ADDRESS + 0x15c0, ADDRESS + 0x16c0, ADDRESS + 0x17c0, ADDRESS + 0x10e0, ADDRESS + 0x11e0, ADDRESS + 0x12e0, ADDRESS + 0x13e0, ADDRESS + 0x14e0, ADDRESS + 0x15e0, ADDRESS + 0x16e0, ADDRESS + 0x17e0};

        static uint8_t * row(uint8_t y) {
            return (uint8_t*)row_addresses[y];
        }

        constexpr static uint16_t rowblock_addresses[24] = {row_addresses[0], row_addresses[8], row_addresses[16], row_addresses[24], row_addresses[32], row_addresses[40], row_addresses[48], row_addresses[56], row_addresses[64], row_addresses[72], row_addresses[80], row_addresses[88], row_addresses[96], row_addresses[104], row_addresses[112], row_addresses[120], row_addresses[128], row_addresses[136], row_addresses[144], row_addresses[152], row_addresses[160], row_addresses[168], row_addresses[176], row_addresses[184]};

        constexpr static uint16_t attr_addresses[24] = {
            ADDRESS + 6144 +  0 * 32, ADDRESS + 6144 +  1 * 32, ADDRESS + 6144 +  2 * 32, ADDRESS + 6144 +  3 * 32,
            ADDRESS + 6144 +  4 * 32, ADDRESS + 6144 +  5 * 32, ADDRESS + 6144 +  6 * 32, ADDRESS + 6144 +  7 * 32,
            ADDRESS + 6144 +  8 * 32, ADDRESS + 6144 +  9 * 32, ADDRESS + 6144 + 10 * 32, ADDRESS + 6144 + 11 * 32,
            ADDRESS + 6144 + 12 * 32, ADDRESS + 6144 + 13 * 32, ADDRESS + 6144 + 14 * 32, ADDRESS + 6144 + 15 * 32,
            ADDRESS + 6144 + 16 * 32, ADDRESS + 6144 + 17 * 32, ADDRESS + 6144 + 18 * 32, ADDRESS + 6144 + 19 * 32,
            ADDRESS + 6144 + 20 * 32, ADDRESS + 6144 + 21 * 32, ADDRESS + 6144 + 22 * 32, ADDRESS + 6144 + 23 * 32,
        };
        static uint8_t * attr_row(uint8_t y) {
            return (uint8_t*)attr_addresses[y / 8];
        }
    };

    using Screen = _Screen<0x4000>;

    struct AlignedSprite {
        struct Config {
            uint8_t width;
            uint8_t height;
        };

        template <Config cfg>
        struct Instance {
            static_assert(cfg.width % 8 == 0, "width must be multiple of 8");
            static_assert(cfg.height % 8 == 0, "height must be multiple of 8");

            uint8_t bitmap[cfg.height][cfg.width / 8];
            uint8_t colormap[cfg.height / 8][cfg.width / 8];

            template <typename ScreenType>
            void paint(uint8_t x_in, uint8_t y_in) __attribute__((noinline)) {
                auto *rowblock = &ScreenType::rowblock_addresses[y_in];
                #pragma unroll
                for (uint8_t cy = 0; cy < cfg.height; ) {
                    uint8_t *ptr = (uint8_t*)*rowblock++ + x_in;
                    #pragma unroll
                    for (uint8_t i = 0; i < 8; ++i) {
                        memcpy(ptr, &bitmap[cy + i][0], cfg.width / 8);
                        ptr += ScreenType::SCANLINE_SIZE;
                    }
                    cy += 8;
                }

                uint8_t *attr_ptr = ScreenType::attr_row(y_in * 8) + x_in;
                #pragma unroll
                for (uint8_t cy = 0; cy < cfg.height / 8; ++cy ) {
                    memcpy(attr_ptr, &colormap[cy][0], cfg.width / 8);
                    attr_ptr += ScreenType::ROW_SIZE;
                }
            }

            constexpr uint8_t get_width() const { return cfg.width; }
            constexpr uint8_t get_height() const { return cfg.height; }
        };

        template <Config cfg>
        struct MaskedInstance {
            static_assert(cfg.width % 8 == 0, "width must be multiple of 8");
            static_assert(cfg.height % 8 == 0, "height must be multiple of 8");

            uint8_t bitmap[cfg.height][cfg.width / 8];
            uint8_t mask[cfg.height][cfg.width / 8];

            template <typename ScreenType>
            void paint(uint8_t x_in, uint8_t y_in) __attribute__((noinline)) {
                auto *rowblock = &ScreenType::rowblock_addresses[y_in];
                for (uint8_t cy = 0; cy < cfg.height; ) {
                    uint8_t *ptr = (uint8_t*)*rowblock++ + x_in;
                    #pragma unroll
                    for (uint8_t i = 0; i < 8; ++i) {
                        #pragma unroll
                        for (uint8_t x = 0; x < cfg.width / 8; ++x) {
                            ptr[x] = (ptr[x] & mask[cy + i][x]) | bitmap[cy + i][x];
                        }
                        ptr += ScreenType::SCANLINE_SIZE;
                    }
                    cy += 8;
                }

            }

            uint8_t get_width() const { return cfg.width; }
            uint8_t get_height() const { return cfg.height; }
        };
    };
};

