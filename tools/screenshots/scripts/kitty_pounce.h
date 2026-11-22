// Kitty Pounce: start, play with a simple autopilot
#define SCRIPT_FRAMES 4500
static bool ap = true, tookOver = false;
static bool dogNear(int lane) {
  for (uint8_t i = 0; i < MAX_OBJ; i++)
    if (objs[i].type == DOG && objs[i].lane == lane && (objs[i].x >> 4) < 55) return true;
  return false;
}
uint8_t script(uint32_t f) {
  if (f == 40) return B_HASH;
  if (state != PLAY || !ap || (f % 2) || pounceT) return 0;
  if (dogNear(catLane)) {
    if (catLane > 0 && !dogNear(catLane - 1)) return B_UP;
    return B_DOWN;
  }
  int bestLane = -1, bestX = 999;
  for (uint8_t i = 0; i < MAX_OBJ; i++)
    if ((objs[i].type == MOUSE || objs[i].type == FISH) && (objs[i].x >> 4) < bestX) { bestX = objs[i].x >> 4; bestLane = objs[i].lane; }
  if (bestLane < 0) return 0;
  if (bestLane < catLane && !dogNear(catLane - 1)) return B_UP;
  if (bestLane > catLane && !dogNear(catLane + 1)) return B_DOWN;
  if (bestLane == catLane && bestX < CAT_X + 30) return B_HASH;
  return 0;
}
void after(uint32_t f) {
  if (f == 30) shot("pounce_title");
  if (f == 1965) shot("pounce_play");   // cat catching a mouse, dog on the next shelf
  if (f == 3000) ap = false;          // stop playing so the game ends
  static uint8_t overFrames = 0;
  if (state == GAMEOVER && !tookOver && ++overFrames > 2 && (millis() / 500) % 2) { shot("pounce_gameover"); tookOver = true; }
}
