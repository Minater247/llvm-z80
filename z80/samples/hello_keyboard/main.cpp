
#include "zx.h"

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
        ZX::Keyboard::KEY_SPACE, 4
    );
}

int main()
{
  ZX::ScopedDisableInterrupts DI;

  ZX::Console::at(2, 18);
  ZX::Console::print("Use arrow keys or OPQA.");
  ZX::Console::at(2, 19);
  ZX::Console::print("Press K for Kempston joystick.");
  ZX::Console::at(2, 20);
  ZX::Console::print("Space or FIRE to clear.");
  ZX::Console::at(2, 21);
  ZX::Console::print("Press S to START!");

  bool kempston_enabled = false;

  while (!ZX::Keyboard::KEY_S.is_pressed()) {
      __asm__ volatile("nop");
      if (!kempston_enabled && ZX::Keyboard::KEY_K.is_pressed()) {
          kempston_enabled = true;
          ZX::Console::at(2, 19);
          ZX::Console::print("Kempston Joystick is enabled! ");
      }
      if (kempston_enabled && (ZX::Kempston::read() & (1 << 4)))
          break;
  }

  uint8_t x = 128;
  uint8_t y = 96;

  while (true) {
      uint8_t mask = 0;
      for (int i = 0 ; i < 50; ++i) {
          mask |= read_keypress_mask();
          if (kempston_enabled)
              mask |= ZX::Kempston::read();
          else if (ZX::Keyboard::KEY_K.is_pressed())
              kempston_enabled = true;
      }

      if (mask & (1 << 0))
          ++x;
      if (mask & (1 << 1))
          --x;
      if (mask & (1 << 2)) {
          if (y == 191)
              y = 0;
          else
              ++y;
      }
      if (mask & (1 << 3)) {
          if (y == 0)
              y = 191;
          else
              --y;
      }

      if (mask & (1 << 4))
          memset(ZX::Screen::ptr(), 0, 32 * 192);
        
      ZX::Screen::set(x, y, 1);
  }
  return 0;
}

