#include <stdint.h>

void dsply(char *name) {
  volatile uint8_t *vram = (volatile uint8_t *)0xFF00;
  while (*name) {
    *vram = *name++;
  }
}

int main(int argc, char **argv) {
  dsply(*argv);

  return 7;
}
