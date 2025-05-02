#pragma once

namespace ZX {
    namespace AY {
        static inline void write_register(uint8_t reg, uint8_t value) {
            uint8_t b = 0xff;
            uint8_t c = 0xfd;
            __asm__ volatile (
                "out\t(c), a"
                : : "a"(reg), "b"(b), "c"(c) :
            );
            b = 0xbf;
            __asm__ volatile (
                "out\t(c), a"
                : : "a"(value), "b"(b), "c"(c) :
            );
        }

        enum Envelope : uint8_t {
            HOLD_LOW            = 0x00,
            HOLD_HIGH           = 0x01,
            RAMP_DOWN           = 0x02,
            RAMP_UP             = 0x03,
            TRIANGLE            = 0x04,
            INV_TRIANGLE        = 0x05,
            ZIGZAG              = 0x06,
            INV_ZIGZAG          = 0x07,

            HOLD_LOW_LOOP       = 0x08,
            HOLD_HIGH_LOOP      = 0x09,
            RAMP_DOWN_LOOP      = 0x0A,
            RAMP_UP_LOOP        = 0x0B,
            TRIANGLE_LOOP       = 0x0C,
            INV_TRIANGLE_LOOP   = 0x0D,
            ZIGZAG_LOOP         = 0x0E,
            INV_ZIGZAG_LOOP     = 0x0F,
        };

        static uint8_t _MIXER = 0x3F;

        template <uint8_t CHANNEL>
        struct _Channel {
            static void play_tone(uint16_t period, uint8_t volume) __attribute__((noinline)) {
                write_register(CHANNEL * 2 + 0, period & 0xff);
                write_register(CHANNEL * 2 + 1, period >> 8);
                write_register(8 + CHANNEL, volume);
                // enable tone
                _MIXER &= ~(1 << CHANNEL);
                write_register(7, _MIXER);
            }

            void mute() {
                _MIXER |= (1 << CHANNEL);
                write_register(7, _MIXER);
            }


            static void set_volume(uint8_t volume) {
                write_register(8 + CHANNEL, volume);
                if (volume == 0)
                    mute<CHANNEL>();
            }

            static void play_envelope(uint16_t tone_period, uint16_t envelope_period, Envelope envelope) __attribute__((noinline)) {
                write_register(CHANNEL * 2 + 0, tone_period & 0xff);
                write_register(CHANNEL * 2 + 1, tone_period >> 8);
                write_register(11, envelope_period & 0xFF);
                write_register(12, (envelope_period >> 8) & 0xFF);
                write_register(13, envelope);
                write_register(8 + CHANNEL, 0x10);
                _MIXER &= ~(1 << CHANNEL);
                write_register(7, _MIXER);
            }
        };

        _Channel<0> ChannelA;
        _Channel<1> ChannelB;
        _Channel<2> ChannelC;

        void mute_all() {
            _MIXER = 0x3F;
            write_register(7, _MIXER);
        }
    };
} // namespace ZX

