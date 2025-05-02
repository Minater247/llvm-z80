#pragma once

namespace ZX {
    namespace Keyboard {
        struct _PortReader {
            uint8_t port_read[8] = {0};
            uint8_t port_value[8] = {0};
            static uint8_t port2index(uint8_t port) __attribute__((always_inline)) {
                switch(port) {
                    case 0x7f: return 0;
                    case 0xbf: return 1;
                    case 0xdf: return 2;
                    case 0xef: return 3;
                    case 0xf7: return 4;
                    case 0xfb: return 5;
                    case 0xfd: return 6;
                    default: return 7;
                }
            }
            uint8_t read_port(uint8_t port)  __attribute__((always_inline)) {
                uint8_t port_index = port2index(port);
                if (port_read[port_index])
                    return port_value[port_index];
                port_read[port_index] = 1;
                uint8_t a;
                __asm__ volatile (
                    "in\ta, (0xfe)"
                    : "=a"(a)
                    : "a"(port)
                    :
                );
                port_value[port_index] = a;
                return a;
            }
        };

        template <uint8_t PORT>
        static uint8_t _read_row() {
            uint8_t a;
            __asm__ volatile (
                "in\ta, (0xfe)"
                : "=a"(a)
                : "a"(PORT)
                :
            );
            return a;
        }

        template <char C, uint8_t PORT, uint8_t BIT, bool SHIFT = false> struct Key {
            constexpr char get_character() const { return C; }
            constexpr uint8_t get_port() const { return PORT; }
            constexpr uint8_t get_mask() const { return 1 << BIT; }
            constexpr bool shifted() const { return SHIFT; }

            bool is_pressed() const {
                if (SHIFT) {
                    if (_read_row<0xfe>() & 1)
                        return false;
                }
                return !(_read_row<PORT>() & get_mask());
            }

            bool is_pressed(_PortReader& pr) const {
                if (SHIFT) {
                    if (pr.read_port(0xfe) & 1)
                        return false;
                }
                return !(pr.read_port(PORT) & (get_mask()));
            }
        };

        static Key<'1', 0xf7, 0> KEY_1;
        static Key<'2', 0xf7, 1> KEY_2;
        static Key<'3', 0xf7, 2> KEY_3;
        static Key<'4', 0xf7, 3> KEY_4;
        static Key<'5', 0xf7, 4> KEY_5;

        static Key<'6', 0xef, 4> KEY_6;
        static Key<'7', 0xef, 3> KEY_7;
        static Key<'8', 0xef, 2> KEY_8;
        static Key<'9', 0xef, 1> KEY_9;
        static Key<'0', 0xef, 0> KEY_0;

        static Key<'q', 0xfb, 0> KEY_Q;
        static Key<'w', 0xfb, 1> KEY_W;
        static Key<'e', 0xfb, 2> KEY_E;
        static Key<'r', 0xfb, 3> KEY_R;
        static Key<'t', 0xfb, 4> KEY_T;

        static Key<'y', 0xdf, 4> KEY_Y;
        static Key<'u', 0xdf, 3> KEY_U;
        static Key<'i', 0xdf, 2> KEY_I;
        static Key<'o', 0xdf, 1> KEY_O;
        static Key<'p', 0xdf, 0> KEY_P;

        static Key<'a', 0xfd, 0> KEY_A;
        static Key<'s', 0xfd, 1> KEY_S;
        static Key<'d', 0xfd, 2> KEY_D;
        static Key<'f', 0xfd, 3> KEY_F;
        static Key<'g', 0xfd, 4> KEY_G;

        static Key<'h', 0xbf, 4> KEY_H;
        static Key<'j', 0xbf, 3> KEY_J;
        static Key<'k', 0xbf, 2> KEY_K;
        static Key<'l', 0xbf, 1> KEY_L;
        static Key<' ', 0xbf, 0> KEY_ENTER;

        static Key<'_', 0xfe, 0> KEY_SHIFT;
        static Key<'z', 0xfe, 1> KEY_Z;
        static Key<'x', 0xfe, 2> KEY_X;
        static Key<'c', 0xfe, 3> KEY_C;
        static Key<'v', 0xfe, 4> KEY_V;

        static Key<'b', 0x7f, 4> KEY_B;
        static Key<'n', 0x7f, 3> KEY_N;
        static Key<'m', 0x7f, 2> KEY_M;
        static Key<' ', 0x7f, 1> KEY_SYM;
        static Key<' ', 0x7f, 0> KEY_SPACE;

        static Key<' ', 0xef, 2, true> KEY_RIGHT;
        static Key<' ', 0xef, 3, true> KEY_UP;
        static Key<' ', 0xef, 4, true> KEY_DOWN;
        static Key<' ', 0xf7, 4, true> KEY_LEFT;

        template <typename KEY>
        static inline uint8_t _pressed_mask(_PortReader& pr, KEY key, uint8_t bit_index)
        {
            return key.is_pressed(pr) ? (1 << bit_index) : 0;
        }

        template <typename KEY, class... Rest>
        static inline uint8_t _pressed_mask(_PortReader& pr, KEY key, uint8_t bit_index, const Rest&... rest)
        {
            uint8_t mask = _pressed_mask(pr, rest...);
            if (key.is_pressed(pr))
                mask |= (1 << bit_index);
            return mask;
        }

        template <typename KEY, class... Rest>
        static inline uint8_t pressed_mask(KEY key, uint8_t bit_index, const Rest&... rest)
        {
            _PortReader pr;
            return _pressed_mask(pr, key, bit_index, rest...);
        }
    } // namespace Keyboard
} // namespace ZX

