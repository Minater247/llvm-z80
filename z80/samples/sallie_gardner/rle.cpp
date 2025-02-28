
#include "rle.h"

// This is quite slow right now due to no inlining
// of memset/memcpy and the encoding of length that
// is tailored towards a quick jump offset in the
// assembly solution.
void RLE::decode(const uint8_t *src, uint8_t *dst) {
    while (true) {
        uint8_t control = *src++;

        if (control == 0) {  // End of file marker
            break;
        }

        if (control < (1 + 2 * 24)) {  // 0x00 block
            uint8_t length = 24 - ((uint8_t)(control - 1) / 2);
            memset(dst, 0x00, length);
            dst += length;
        } else if (control < (1 + 2 * 24 + 3 + 2 * 24)) {  // 0xFF block
            uint8_t length = 24 - ((uint8_t)(control - (1 + 2 * 24 + 3)) / 2);
            memset(dst, 0xFF, length);
            dst += length;
        } else {  // Mixed data block
            uint8_t length = 12 - ((uint8_t)(control - (1 + 2 * 24 + 3 + 2 * 24 + 3)) / 2);
            memcpy(dst, src, length);
            dst += length;
            src += length;
        }
    }
}

