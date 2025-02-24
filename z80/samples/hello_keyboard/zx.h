#pragma once

using int8_t = char;
using uint8_t = unsigned char;
using int16_t = int;
using uint16_t = unsigned int;
using size_t = uint16_t;

extern "C" {
  void * memset(void *b, char c, size_t len);
}

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
    };

    namespace Keyboard {
        struct _PortReader {
            uint8_t port_read[8] = {0};
            uint8_t port_value[8] = {0};
            static uint8_t port2index(uint8_t port) __attribute__((always_inline)) {
                switch(port) {
                    case 0x7f: return 0;
                    case 0xbf: return 1;
                    case 0xdf: return 2;
                    case 0xef: return 3;
                    case 0xf7: return 4;
                    case 0xfb: return 5;
                    case 0xfd: return 6;
                    default: return 7;
                }
            }
            uint8_t read_port(uint8_t port)  __attribute__((always_inline)) {
                uint8_t port_index = port2index(port);
                if (port_read[port_index])
                    return port_value[port_index];
                port_read[port_index] = 1;
                uint8_t a;
                __asm__ volatile (
                    "in\ta, (0xfe)"
                    : "=a"(a)
                    : "a"(port)
                    :
                );
                port_value[port_index] = a;
                return a;
            }
        };

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

            bool is_pressed(_PortReader& pr) const {
                if (SHIFT) {
                    if (pr.read_port(0xfe) & 1)
                        return false;
                }
                return !(pr.read_port(PORT) & (get_mask()));
            }
        };

        static Key<'_', 0xfe, 0> KEY_SHIFT;
        static Key<'p', 0xdf, 0> KEY_P;
        static Key<'o', 0xdf, 1> KEY_O;
        static Key<'q', 0xfb, 0> KEY_Q;
        static Key<'a', 0xfd, 0> KEY_A;
        static Key<'s', 0xfd, 1> KEY_S;
        static Key<' ', 0x7f, 0> KEY_SPACE;
        static Key<'5', 0xf7, 4> KEY_5;
        static Key<'6', 0xef, 4> KEY_6;
        static Key<'7', 0xef, 3> KEY_7;
        static Key<'8', 0xef, 2> KEY_8;
        static Key<'9', 0xef, 1> KEY_9;
        static Key<' ', 0xef, 2, true> KEY_RIGHT;
        static Key<' ', 0xef, 3, true> KEY_UP;
        static Key<' ', 0xef, 4, true> KEY_DOWN;
        static Key<' ', 0xf7, 4, true> KEY_LEFT;

        template <typename KEY>
        static inline uint8_t _pressed_mask(_PortReader& pr, KEY key, uint8_t bit_index)
        {
            return key.is_pressed(pr) ? (1 << bit_index) : 0;
        }

        template <typename KEY, class... Rest>
        static inline uint8_t _pressed_mask(_PortReader& pr, KEY key, uint8_t bit_index, const Rest&... rest)
        {
            uint8_t mask = _pressed_mask(pr, rest...);
            if (key.is_pressed(pr))
                mask |= (1 << bit_index);
            return mask;
        }

        template <typename KEY, class... Rest>
        static inline uint8_t pressed_mask(KEY key, uint8_t bit_index, const Rest&... rest)
        {
            _PortReader pr;
            return _pressed_mask(pr, key, bit_index, rest...);
        }
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

        //static void set(uint8_t x, uint8_t y, uint8_t v) __attribute__ ((noinline)) {
        static void set(uint8_t x, uint8_t y, uint8_t v) {
            auto *p = row(y) + (x >> 3);
            static const uint8_t mask[8] = {1 << 7, 1 << 6, 1 << 5, 1 << 4, 1 << 3, 1 << 2, 1 << 1, 1};
            uint8_t bit_mask = mask[x & 7];
            if (v) {
                *p |= bit_mask;
            } else {
                *p &= ~bit_mask;
            }
        }
    };

    using Screen = _Screen<0x4000>;
};


