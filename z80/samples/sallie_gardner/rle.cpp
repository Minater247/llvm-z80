
#include "rle.h"

// This is quite slow right now due to no inlining
// of memset/memcpy
void RLE::decode(const uint8_t *src, uint8_t *dst) {
    while (true) {
        uint8_t control = *src++;

        uint8_t encoded_length = control & 0x3F;
        uint8_t length = 32 - (encoded_length - 1);

        if (control & 0b10000000) {  // 0x00 repetition
            memset(dst, 0x00, length);
            dst += length;
        } else if (control & 0b01000000) {  // 0xFF repetition
            if (!encoded_length)
                break;
            memset(dst, 0xFF, length);
            dst += length;
        } else {  // Raw data
            memcpy(dst, src, length);
            dst += length;
            src += length;
        }
    }
}

