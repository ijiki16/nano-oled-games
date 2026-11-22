// Runs a sketch frame by frame on the PC and dumps chosen frames as PBM images.
// Built by build.sh: GAME_INO = path to the .ino, SCRIPT = per-game input/screenshot script.
#include <Arduino.h>
#include <U8g2lib.h>

uint32_t g_ms = 0;
uint8_t g_buttons = 0;
uint8_t g_eeprom[1024];
float g_tempC = 17.2f, g_hum = 54.0f;   // the real readings from the user's room

extern "C" uint8_t u8x8_byte_arduino_hw_i2c(u8x8_t*, uint8_t, uint8_t, void*) { return 1; }
extern "C" uint8_t u8x8_gpio_and_delay_arduino(u8x8_t*, uint8_t, uint8_t, void*) { return 1; }

void u8x8_SetPin_HW_I2C(u8x8_t*, uint8_t, uint8_t, uint8_t) {}

// Page-mode sketches render into one full buffer instead, so the whole frame is captured
#define U8G2_SSD1306_128X64_NONAME_2_HW_I2C U8G2_SSD1306_128X64_NONAME_F_HW_I2C

#include GAME_INO

static const char* g_outdir;
static void shot(const char* name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.pbm", g_outdir, name);
  FILE* f = fopen(path, "w");
  uint8_t* buf = u8g2.getBufferPtr();
  fprintf(f, "P1\n128 64\n");
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 128; x++) fputc((buf[(y / 8) * 128 + x] >> (y % 8)) & 1 ? '1' : '0', f);
    fputc('\n', f);
  }
  fclose(f);
  printf("shot %s at frame\n", name);
}

#include SCRIPT   // defines: uint8_t script(uint32_t frame) and void after(uint32_t frame)

int main(int argc, char** argv) {
  g_outdir = argv[1];
  memset(g_eeprom, 0xFF, sizeof(g_eeprom));
  srand(1);
  setup();
  for (uint32_t f = 0; f < SCRIPT_FRAMES; f++) {
    g_buttons = script(f);
    loop();
    after(f);
    g_ms += 33;
  }
  return 0;
}
