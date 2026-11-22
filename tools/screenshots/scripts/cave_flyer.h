// Cave Flyer: start, fly with an autopilot, then let it crash
#define SCRIPT_FRAMES 1600
static bool autopilot = true, tookOver = false;
uint8_t script(uint32_t f) {
  if (f == 40) return B_HASH;
  if (state != PLAY || !autopilot) return 0;
  // aim for the middle of the gap a few columns ahead, dodging rocks
  uint8_t c = (SHIP_X + 4 + scrollPx) / SEG_W + 3;
  uint8_t i = (head + c) % NSEG;
  int target = (caveTop[i] + caveBot[i]) / 2 - 2;
  for (uint8_t k = 1; k <= 5; k++) {
    uint8_t j = (head + c - 2 + k) % NSEG;
    if (rockY[j] >= 0) {
      int above = rockY[j] - caveTop[j], below = caveBot[j] - (rockY[j] + ROCK_H);
      target = above > below ? caveTop[j] + above / 2 - 2 : rockY[j] + ROCK_H + below / 2 - 2;
      break;
    }
  }
  int y = (shipY >> 4) + (shipV * 5) / 16;
  return y > target ? B_UP : 0;
}
void after(uint32_t f) {
  if (f == 30) shot("cave_title");
  if (f == 900) { shot("cave_play"); autopilot = false; }
  static uint8_t overFrames = 0;
  if (state == GAMEOVER && !tookOver && ++overFrames > 2 && (millis() / 500) % 2) { shot("cave_gameover"); tookOver = true; }
}
