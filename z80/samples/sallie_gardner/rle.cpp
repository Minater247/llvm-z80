
#include "rle.h"

void RLE::decode(const uint8_t *src, uint8_t *dst) {
    // XXX no C++ code here, also our encoding is now way too
    // tied to the assembler decoder
    return rle_decode(src, dst);
}

