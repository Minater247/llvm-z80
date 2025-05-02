
#include "zx.h"

using MaskedSprite = ZX::Sprite::MaskedInstance<
    ZX::Sprite::Config {
        .width = 32,
        .height = 32,
    }
>;

static constexpr MaskedSprite SPRITE1(
    {{
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000001111100000000000),
        BIN32(0b00000000000000110000011000000000),
        BIN32(0b00000000000001000000000100000000),
        BIN32(0b00000000000010000000000010000000),
        BIN32(0b00000000000100000000000001000000),
        BIN32(0b00000000000100001100011001000000),
        BIN32(0b00000000001000010010100100100000),
        BIN32(0b00000000001000010110101100100000),
        BIN32(0b00000000001000011110111100100000),
        BIN32(0b00000000011000000000000000110000),
        BIN32(0b00000000100100000111110001001000),
        BIN32(0b00000000100100000100010001001000),
        BIN32(0b00000000100010000011100010001000),
        BIN32(0b00001000100000000000000000001000),
        BIN32(0b00010100010000000000000000010000),
        BIN32(0b00100100011100000000000001100000),
        BIN32(0b00100011100000000000000001000000),
        BIN32(0b00100001000000000000000001000000),
        BIN32(0b00100000100000000000000010000000),
        BIN32(0b00010000000000000000000010000000),
        BIN32(0b00010000000000000000000100000000),
        BIN32(0b00001000000000000000000100000000),
        BIN32(0b00001000000000000000001000000000),
        BIN32(0b00000110000000000000010000000000),
        BIN32(0b00000001100000000001100000000000),
        BIN32(0b00000000011111111110000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
    }},
    {{
        BIN32(0b11111111111111111111111111111111),
        BIN32(0b11111111111111111111111111111111),
        BIN32(0b11111111111111100000001111111111),
        BIN32(0b11111111111110000000000011111111),
        BIN32(0b11111111111100000000000001111111),
        BIN32(0b11111111111000000000000000111111),
        BIN32(0b11111111110000000000000000011111),
        BIN32(0b11111111110000000000000000011111),
        BIN32(0b11111111100000000000000000001111),
        BIN32(0b11111111100000000000000000001111),
        BIN32(0b11111111100000000000000000001111),
        BIN32(0b11111111000000000000000000000111),
        BIN32(0b11111110000000000000000000000011),
        BIN32(0b11111110000000000000000000000011),
        BIN32(0b11111110000000000000000000000011),
        BIN32(0b11100010000000000000000000000011),
        BIN32(0b11000000000000000000000000000011),
        BIN32(0b10000000000000000000000000000011),
        BIN32(0b10000000000000000000000000000111),
        BIN32(0b10000000000000000000000000001111),
        BIN32(0b10000000000000000000000000011111),
        BIN32(0b10000000000000000000000000011111),
        BIN32(0b10000000000000000000000000111111),
        BIN32(0b11000000000000000000000000111111),
        BIN32(0b11000000000000000000000001111111),
        BIN32(0b11100000000000000000000001111111),
        BIN32(0b11100000000000000000000011111111),
        BIN32(0b11110000000000000000000111111111),
        BIN32(0b11111100000000000000001111111111),
        BIN32(0b11111111000000000000111111111111),
        BIN32(0b11111111111111111111111111111111),
        BIN32(0b11111111111111111111111111111111),
      }}
);

using BlockSprite = ZX::Sprite::Instance<
    ZX::Sprite::Config {
        .width = 32,
        .height = 32,
    }
>;

static constexpr BlockSprite SPRITE2(
    {{
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000001111100000000000),
        BIN32(0b00000000000000110000011000000000),
        BIN32(0b00000000000001000000000100000000),
        BIN32(0b00000000000010000000000010000000),
        BIN32(0b00000000000100000000000001000000),
        BIN32(0b00000000000100001100011001000000),
        BIN32(0b00000000001000010010100100100000),
        BIN32(0b00000000001000010110101100100000),
        BIN32(0b00000000001000011110111100100000),
        BIN32(0b00000000011000000000000000110000),
        BIN32(0b00000000100100000111110001001000),
        BIN32(0b00000000100100000100010001001000),
        BIN32(0b00000000100010000011100010001000),
        BIN32(0b00001000100000000000000000001000),
        BIN32(0b00010100010000000000000000010000),
        BIN32(0b00100100011100000000000001100000),
        BIN32(0b00100011100000000000000001000000),
        BIN32(0b00100001000000000000000001000000),
        BIN32(0b00100000100000000000000010000000),
        BIN32(0b00010000000000000000000010000000),
        BIN32(0b00010000000000000000000100000000),
        BIN32(0b00001000000000000000000100000000),
        BIN32(0b00001000000000000000001000000000),
        BIN32(0b00000110000000000000010000000000),
        BIN32(0b00000001100000000001100000000000),
        BIN32(0b00000000011111111110000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
        BIN32(0b00000000000000000000000000000000),
    }}
);

uint8_t read_keypress_mask() __attribute__((noinline));
uint8_t read_keypress_mask() {
    return ZX::Keyboard::pressed_mask(
        ZX::Keyboard::KEY_RIGHT, 0,
        ZX::Keyboard::KEY_P, 0,
        ZX::Keyboard::KEY_LEFT, 1,
        ZX::Keyboard::KEY_O, 1,
        ZX::Keyboard::KEY_DOWN, 2,
        ZX::Keyboard::KEY_A, 2,
        ZX::Keyboard::KEY_UP, 3,
        ZX::Keyboard::KEY_Q, 3,
        ZX::Keyboard::KEY_SPACE, 4,
        ZX::Keyboard::KEY_7, 0,
        ZX::Keyboard::KEY_6, 1,
        ZX::Keyboard::KEY_8, 2,
        ZX::Keyboard::KEY_9, 3,
        ZX::Keyboard::KEY_0, 4
    );
}

int main() {
    ZX::ScopedDisableInterrupts DI;

    // set screen with black ink and white paper and white border
    ZX::Console::border(7);
    memset(ZX::Screen::attr_row(0), 0x38, 32*192/8);
    ZX::Console::set_print_attr(0x38);

    ZX::Console::at(2, 18);
    ZX::Console::print("Use arrow keys or OPQA.");
    ZX::Console::at(2, 19);
    ZX::Console::print("Press K for Kempston joystick.");
    ZX::Console::at(2, 20);
    ZX::Console::print("Press 1 for mask, 2 for block.");
    ZX::Console::at(2, 21);
    ZX::Console::print("Press S to START!");

    bool kempston_enabled = false;
    bool use_sprite1 = true;

    while (!ZX::Keyboard::KEY_S.is_pressed()) {
        __asm__ volatile("nop");
        if (!kempston_enabled && ZX::Keyboard::KEY_K.is_pressed()) {
            kempston_enabled = true;
            ZX::Console::at(2, 19);
            ZX::Console::print("Kempston Joystick is enabled! ");
        }
        if (kempston_enabled && (ZX::Kempston::read() & (1 << 4)))
            break;
        if (!use_sprite1 && ZX::Keyboard::KEY_1.is_pressed()) {
            use_sprite1 = true;
            ZX::Console::at(2, 20);
            ZX::Console::print("Masked blit selected.          ");
        } else if (use_sprite1 && ZX::Keyboard::KEY_2.is_pressed()) {
            use_sprite1 = false;
            ZX::Console::at(2, 20);
            ZX::Console::print("Block blit selected.           ");
        }
    }

    for (uint16_t i = 440; i >= 220; --i) {
        ZX::AY::ChannelA.play_tone(i, 15);
        for (int j = 0; j < 200; ++j )
            __asm__ volatile ("nop");
    }
    ZX::AY::ChannelA.mute();

    uint8_t x = 128;
    uint8_t y = 96;

    while (true) {
        uint8_t mask = 0;
        for (int i = 0 ; i < 1; ++i) {
            mask |= read_keypress_mask();
            if (kempston_enabled) {
                mask |= ZX::Kempston::read();
            } else if (!kempston_enabled && ZX::Keyboard::KEY_K.is_pressed()) {
                kempston_enabled = true;
                ZX::Console::at(2, 19);
                ZX::Console::print("Kempston Joystick is enabled! ");
            }

            if (!use_sprite1 && ZX::Keyboard::KEY_1.is_pressed()) {
                use_sprite1 = true;
                ZX::Console::at(2, 20);
                ZX::Console::print("Masked blit selected.         ");
            } else if (use_sprite1 && ZX::Keyboard::KEY_2.is_pressed()) {
                use_sprite1 = false;
                ZX::Console::at(2, 20);
                ZX::Console::print("Block blit selected.          ");
            }
        }

        if (mask & (1 << 0) && x < 256 - 33)
            ++x;
        if (x && mask & (1 << 1))
            --x;
        if (mask & (1 << 2) && y < 192 - 32)
            ++y;
        if (y && mask & (1 << 3))
            --y;

        if (mask & (1 << 4)) {
            ZX::AY::ChannelA.play_envelope(1600, 1000, ZX::AY::Envelope::RAMP_DOWN);
        }

        if (use_sprite1)
            SPRITE1.paint<ZX::Screen>(x, y);
        else
            SPRITE2.paint<ZX::Screen>(x, y);
    }

    return 0;
}

