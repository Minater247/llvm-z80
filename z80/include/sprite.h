#pragma once

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

    struct Sprite {
        struct Config {
            uint8_t width;
            uint8_t height;
        };

        template <Config cfg>
        struct Instance {
            static_assert(cfg.width % 8 == 0, "width must be multiple of 8");
            static_assert(cfg.height % 8 == 0, "height must be multiple of 8");

            constexpr Instance(
                const std::array<std::array<uint8_t, cfg.width / 8>, cfg.height>& in_bitmap
            ) __attribute__((always_inline)) : data {} {
                for (int shift = 0; shift < 8; ++shift) {
                    for (int y = 0; y < cfg.height; ++y) {
                        uint8_t bitmap_remainder = 0;
                        for (int x = 0; x < cfg.width / 8; ++x) {
                            uint8_t new_bitmap_remainder = (in_bitmap[y][x] & (0xff >> (8 - shift))) << (8 - shift);
                            data[shift].bitmap[y][x] = (in_bitmap[y][x] >> shift) | bitmap_remainder;
                            bitmap_remainder = new_bitmap_remainder;
                        }
                        data[shift].bitmap[y][cfg.width / 8] = bitmap_remainder;
                    }
                }
            }

            struct shift_entry {
                uint8_t bitmap[cfg.height][cfg.width / 8 + 1];
            };

            shift_entry data[8];
            std::array<const shift_entry*, 8> shift_address {&data[0], &data[1], &data[2], &data[3], &data[4], &data[5], &data[6], &data[7]};

            template <typename ScreenType>
            void paint(uint8_t x_in, uint8_t y_in) const __attribute__((noinline)) {
                uint8_t xb = x_in >> 3;
                auto *rows = &ScreenType::row_addresses[y_in];
                uint8_t *row_addresses[cfg.height];
                #pragma unroll
                for (uint8_t y = 0; y < cfg.height; ++y)
                    row_addresses[y] = (uint8_t*)*rows++ + xb;
                uint8_t shift = x_in & 7;
                const shift_entry *d = shift_address[shift];
                #pragma unroll
                for (uint8_t cy = 0; cy < cfg.height; ++cy) {
                    uint8_t *ptr = row_addresses[cy];
                    memcpy(ptr, &d->bitmap[cy][0], cfg.width / 8 + 1);
                }
            }

            constexpr uint8_t get_width() const { return cfg.width; }
            constexpr uint8_t get_height() const { return cfg.height; }
        };

        template <Config cfg>
        struct MaskedInstance {
            static_assert(cfg.width % 8 == 0, "width must be multiple of 8");
            static_assert(cfg.height % 8 == 0, "height must be multiple of 8");

            constexpr MaskedInstance(
                const std::array<std::array<uint8_t, cfg.width / 8>, cfg.height>& in_bitmap,
                const std::array<std::array<uint8_t, cfg.width / 8>, cfg.height>& in_mask
            ) __attribute__((always_inline)) : data {} {
                for (int shift = 0; shift < 8; ++shift) {
                    for (int y = 0; y < cfg.height; ++y) {
                        uint8_t bitmap_remainder = 0;
                        uint8_t mask_remainder = 0;
                        for (int x = 0; x < cfg.width / 8; ++x) {
                            uint8_t new_bitmap_remainder = (in_bitmap[y][x] & (0xff >> (8 - shift))) << (8 - shift);
                            uint8_t m = ~in_mask[y][x];
                            uint8_t new_mask_remainder = (m & (0xff >> (8 - shift))) << (8 - shift);
                            data[shift].bitmap[y][x] = (in_bitmap[y][x] >> shift) | bitmap_remainder;
                            data[shift].mask[y][x] = ~((m >> shift) | mask_remainder);
                            bitmap_remainder = new_bitmap_remainder;
                            mask_remainder = new_mask_remainder;
                        }
                        data[shift].bitmap[y][cfg.width / 8] = bitmap_remainder;
                        data[shift].mask[y][cfg.width / 8] = ~mask_remainder;
                    }
                }
            }

            template <typename ScreenType>
            void paint(uint8_t x_in, uint8_t y_in) const __attribute__((noinline)) {
                auto *rows = &ScreenType::row_addresses[y_in];
                uint8_t xb = x_in >> 3;
                uint8_t shift = x_in & 7;
                const shift_entry *d = shift_address[shift];
                for (uint8_t cy = 0; cy < cfg.height; cy += 4) {
                    #pragma unroll
                    for (uint8_t dy = 0; dy < 4; ++dy) {
                        uint8_t *ptr = (uint8_t*)*rows++ + xb;
                        #pragma unroll
                        for (uint8_t x = 0; x < cfg.width / 8 + 1; ++x) {
                            ptr[x] = (ptr[x] & d->mask[cy + dy][x]) | d->bitmap[cy + dy][x];
                        }
                    }
                }

            }

            struct shift_entry {
                uint8_t bitmap[cfg.height][cfg.width / 8 + 1];
                uint8_t mask[cfg.height][cfg.width / 8 + 1];
            };

            shift_entry data[8];
            std::array<const shift_entry*, 8> shift_address {&data[0], &data[1], &data[2], &data[3], &data[4], &data[5], &data[6], &data[7]};
        };
    };
} // namespace ZX

