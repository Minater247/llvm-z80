
#include "zx.h"
#include "rle.h"

#include "horse.h"

int main() {
    ZX::ScopedDisableInterrupts DI;

    const uint8_t *frames[] = {
        // horse_000, // seems to be the same position as horse_010
        horse_001,
        horse_002,
        horse_003,
        horse_004,
        horse_005,
        horse_006,
        horse_007,
        horse_008,
        horse_009,
        horse_010,
    };

    ZX::Console::border(7);
    memset(ZX::Screen::ptr() + 32*192, 0x38, 32*192/8);

    // we use double buffering
    static uint8_t buffer[32*192];

    while(true) {
        for (auto *frame : frames) {
#if 0
            // Pure C++ version is quite slow right now
            RLE::decode(frame, buffer);
#else
            //rle_decode(frame, ZX::Screen::ptr());
            rle_decode(frame, buffer);
#endif

            ZX::enable_interrupts();
            __asm__ volatile("halt");
            ZX::disable_interrupts();

#if 1
            memcpy(ZX::Screen::ptr(), buffer, 32*192);
#endif
        }
    }
}

