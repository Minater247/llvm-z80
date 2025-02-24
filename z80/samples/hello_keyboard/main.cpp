
#include "zx.h"

uint8_t read_keypress_mask() __attribute__((noinline));
uint8_t read_keypress_mask() {
    return ZX::Keyboard::pressed_mask(
        ZX::Keyboard::KEY_LEFT, 0,
        ZX::Keyboard::KEY_O, 0,
        ZX::Keyboard::KEY_RIGHT, 1,
        ZX::Keyboard::KEY_P, 1,
        ZX::Keyboard::KEY_UP, 2,
        ZX::Keyboard::KEY_Q, 2,
        ZX::Keyboard::KEY_DOWN, 3,
        ZX::Keyboard::KEY_A, 3,
        ZX::Keyboard::KEY_SPACE, 4
    );
}

int main()
{
  ZX::Console::at(4, 19);
  ZX::Console::print("Use arrow keys or OPQA.");
  ZX::Console::at(4, 20);
  ZX::Console::print("Space to clear.");
  ZX::Console::at(4, 21);
  ZX::Console::print("Press S to START!");

  while (!ZX::Keyboard::KEY_S.is_pressed())
      __asm__ volatile("nop");

  uint8_t x = 128;
  uint8_t y = 96;

  while (true) {
      uint8_t mask = 0;
      for (int i = 0 ; i < 50; ++i)
          mask |= read_keypress_mask();

      if (mask & (1 << 0))
          --x;
      if (mask & (1 << 1))
          ++x;
      if (mask & (1 << 2)) {
          if (y == 0)
              y = 191;
          else
              --y;
      }
      if (mask & (1 << 3)) {
          if (y == 191)
              y = 0;
          else
              ++y;
      }

      if (mask & (1 << 4))
          memset(ZX::Screen::ptr(), 0, 32 * 192);
        
      ZX::Screen::set(x, y, 1);
  }
  return 0;
}

