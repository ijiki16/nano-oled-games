// Pocket Kitty: visit every activity
#define SCRIPT_FRAMES 1640
uint8_t script(uint32_t f) {
  switch (f) {
    case 160: return B_HASH;   // FEED
    case 330: return B_DOWN;   // PLAY
    case 334: return B_HASH;
    case 900: return B_DOWN;   // NAP
    case 904: return B_HASH;
    case 1050: return B_HASH;  // wake up
    case 1120: return B_DOWN;  // WASH
    case 1124: return B_HASH;
    case 1300: return B_DOWN;  // INFO
    case 1304: return B_HASH;
    case 1400: return B_HASH;  // back home
  }
  if (mode == PLAYING) {
    int yc = (yarnX >> 4) + 4, cc = catX + 12;
    if (yc < cc - 3) return B_UP;
    if (yc > cc + 3) return B_DOWN;
  }
  return 0;
}
void after(uint32_t f) {
  if (f == 220) shot("kitty_feed");
  if (f == 600) shot("kitty_yarn");
  if (f == 820) shot("kitty_yarn_done");
  if (f == 1000) shot("kitty_nap");
  if (f == 1180) shot("kitty_bath");
  if (f == 1330) shot("kitty_info");
  if (f == 1620) shot("kitty_home");   // heart above Mochi, snow in the window
}
