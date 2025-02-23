
#include "zx.h"

int main() {
    uint8_t x = 27;
    uint8_t y = 0;
    int8_t dx = 1;
    int8_t dy = 1;
    for (int i = 0; i < 16384; ++i) {
        ZX::Screen::set(x, y, 1);
        x = (int16_t)x + dx;
        y = (int16_t)y + dy;
        if (x == 255) {
            dx = -1;
        } else if ( x == 0 ) {
            dx = 1;
        }
        if (y == 191) {
            dy = -1;
        } else if ( y == 0 ) {
            dy = 1;
        }
    }
    return 0;
}

