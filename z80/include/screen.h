#pragma once

namespace ZX {
    struct _Screen_helper {
        // calculate pixel row address for given row `y`
        static constexpr uint16_t row_offset(uint8_t y) {
            return ((y & 0xC0) << (uint8_t)5)+((y & 0x07) << (uint8_t)8)+((y & 0x38) << (uint8_t)2);
        }

        // construct address lookup table for start of each pixel row
        static constexpr std::array<uint16_t, 192> make_row_lookup(uint16_t base_address) {
            std::array<uint16_t, 192> ret;
            for ( uint8_t y = 0; y < 192; ++y ) {
                ret[y] = base_address + row_offset(y);
            }
            return ret;
        }

        // construct address lookup table for start of each 8-block of rows
        static constexpr std::array<uint16_t, 24> make_rowblock_lookup(uint16_t base_address) {
            std::array<uint16_t, 24> ret;
            for ( uint8_t y = 0; y < 24; ++y ) {
                ret[y] = base_address + row_offset(y * 8);
            }
            return ret;
        }

        // construct address lookup table for start of each attribute row
        static constexpr std::array<uint16_t, 24> make_attr_row_lookup(uint16_t base_address) {
            std::array<uint16_t, 24> ret;
            for ( uint8_t y = 0; y < 24; ++y ) {
                ret[y] = base_address + 6144 + y * 32;
            }
            return ret;
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

        constexpr static std::array<uint16_t, 192> row_addresses = _Screen_helper::make_row_lookup(ADDRESS);
        constexpr static std::array<uint16_t, 24> rowblock_addresses = _Screen_helper::make_rowblock_lookup(ADDRESS);

        static uint8_t * row(uint8_t y) {
            return (uint8_t*)row_addresses[y];
        }

        static uint8_t * rowblock(uint8_t yb) {
            return (uint8_t*)rowblock_addresses[yb];
        }

        constexpr static std::array<uint16_t, 24> attr_addresses = _Screen_helper::make_attr_row_lookup(ADDRESS);

        static uint8_t * attr_row(uint8_t y) {
            return (uint8_t*)attr_addresses[y / 8];
        }

        // set pixel at (x, y) to value v=0/1
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

    enum : uint16_t {
        SCREEN_ADDRESS = 0x4000,
    };

    using Screen = _Screen<SCREEN_ADDRESS>;
} // namespace ZX

