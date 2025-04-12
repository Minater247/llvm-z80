#pragma once

namespace ZX {
    struct _BigLetters_Helper {
        // Generate a lookup table that maps an 8 bit byte into a 16 bit value
        // where bits have been duplicated, so that 0b10000001 -> 0b1100000000000011, etc.
        // This function is constexpr so is evaluated at compile time
        inline static constexpr std::array<uint16_t, 256> _generate_double_bits_lookup_table() {
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
        // XXX move this into its own class
        static constexpr std::array<uint16_t, 256> _double_bits_lookup_table = _BigLetters_Helper::_generate_double_bits_lookup_table();
    };
} // namespace ZX

