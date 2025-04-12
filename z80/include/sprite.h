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
} // namespace ZX

