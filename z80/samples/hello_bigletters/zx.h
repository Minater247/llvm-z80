#pragma once

using int8_t = char;
using uint8_t = unsigned char;
using int16_t = int;
using uint16_t = unsigned int;
using size_t = uint16_t;

extern "C" {
    void *memset(void *s, uint8_t c, uint16_t n);
    void *memcpy(void *dest, const void *src, uint16_t n);
};

namespace std {
  template <typename T, size_t N>
  struct array {
      T data[N];  // Fixed-size array storage

      // Access operators
      constexpr T& operator[](size_t index) { return data[index]; }
      constexpr const T& operator[](size_t index) const { return data[index]; }

      // Get size of the array
      constexpr size_t size() const { return N; }

      // Get a pointer to the underlying data
      constexpr T* begin() { return data; }
      constexpr const T* begin() const { return data; }
      constexpr T* end() { return data + N; }
      constexpr const T* end() const { return data + N; }

      // Direct assignment
      constexpr array& operator=(const array& other) {
          for (size_t i = 0; i < N; i++) {
              data[i] = other.data[i];
          }
          return *this;
      }
  };
}

namespace ZX {

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

    // Generate a lookup table that maps an 8 bit byte into a 16 bit value
    // where bits have been duplicated, so that 0b10000001 -> 0b1100000000000011, etc.
    // This function is constexpr so is evaluated at compile time
    inline constexpr std::array<uint16_t, 256> _generate_double_bits_lookup_table() {
        std::array<uint16_t, 256> ret;
        uint8_t i = 0;
        do {
            uint16_t v = 0;
            if (i & 0b00000001)
              v |= 0b0000000000000011;
            if (i & 0b00000010)
              v |= 0b0000000000001100;
            if (i & 0b00000100)
              v |= 0b0000000000110000;
            if (i & 0b00001000)
              v |= 0b0000000011000000;
            if (i & 0b00010000)
              v |= 0b0000001100000000;
            if (i & 0b00100000)
              v |= 0b0000110000000000;
            if (i & 0b01000000)
              v |= 0b0011000000000000;
            if (i & 0b10000000)
              v |= 0b1100000000000000;
            // because of little endian reverse bytes
            ret[i] = (v & 0xff) << 8 | v >> 8;
        } while (++i != 0);
        return ret;
    }

    struct ROM {
      static const uint8_t *character_data(char c) {
        return (const uint8_t*)0x3D00 + (c - 32) * 8;
      }
    };

    struct BigLetters {
        static void _draw(int xi, int y, uint8_t c) {
            const uint8_t *cp = ROM::character_data(c);
            #pragma unroll(1)
            for ( int i = 0; i < 8; ++i ) {
                uint16_t v = _double_bits_lookup_table[*cp++];
                uint16_t *wptr = (uint16_t*)(Screen::row(y) + xi);
                *wptr = v;
                ++y;
                wptr = (uint16_t*)(Screen::row(y) + xi);
                *wptr = v;
                ++y;
            }
        }

        static void draw(int xi, int y, uint8_t c) __attribute__((noinline)) {
            _draw(xi, y, c);
        }

        static void print(int xi, int y, const char *str) __attribute__((noinline)) {
            while (*str) {
                _draw(xi, y, *str++);
                xi += 2;
            }
        }

        // evaluated at compile time
        static constexpr std::array<uint16_t, 256> _double_bits_lookup_table = _generate_double_bits_lookup_table();
    };
};

