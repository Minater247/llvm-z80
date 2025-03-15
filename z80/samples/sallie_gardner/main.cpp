
#include "zx.h"
#include "rle.h"

#include "horse.h"

int main() {
    ZX::ScopedDisableInterrupts DI;

    const uint8_t *frames[] = {
        // horse_000, // seems to be the same position as horse_010
//        horse_001,
        horse_002,
        horse_003,
        horse_004,
        horse_005,
        horse_006,
        horse_007,
        horse_008,
        horse_009,
        horse_010,
        horse_011,
    };

    ZX::Console::border(7);
    memset(ZX::Screen::ptr() + 32*192, 0x38, 32*192/8);

    rle_decode(horse_001, ZX::Screen::ptr());

    ZX::enable_interrupts();
    __asm__ volatile("halt");
    ZX::disable_interrupts();

    for ( int i = 0; i < 200; ++i ) __asm__ volatile("nop");

    while(true) {
        for (auto *frame : frames) {
            rle_decode(frame, ZX::Screen::ptr());

            ZX::enable_interrupts();
            __asm__ volatile("halt");
            ZX::disable_interrupts();

            // timing hack
            for ( int i = 0; i < 165; ++i ) __asm__ volatile("nop");
        }
    }

    return 0;
}

