#pragma once

#include "zx.h"

extern "C" {
  // our implementation in assemblt from rle_decode.s
  void rle_decode(const uint8_t *src, uint8_t *dst);
}

struct RLE {
    static void decode(const uint8_t *src, uint8_t *dst);
};

