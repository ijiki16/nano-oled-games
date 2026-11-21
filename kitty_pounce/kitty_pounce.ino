/*
  KITTY POUNCE - for Arduino Nano + 1.3" SSD1306 OLED with 4 buttons

  A cat guards three shelves. Mice run in from the right: pounce on them
  before they get past! Dogs charge in too: get out of their shelf.
  Fish are a treat: grab one to win back a heart.

  Controls
    ^ / v    jump up / down a shelf
    #        pounce (start / retry on menus)
    *        pause / resume

  Rules
    +1 point per mouse caught, +3 per fish
    lose a heart when a mouse escapes or a dog hits you
    3 hearts; game over at 0
    Hold v while powering on to reset the best score.

  Wiring
    OLED SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
    K1 (^) -> D12, K2 (v) -> D11, K3 (#) -> D10, K4 (*) -> D9
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include "sprites.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---- Buttons ---------------------------------------------------------------
const uint8_t PIN_UP = 12, PIN_DOWN = 11, PIN_HASH = 10, PIN_STAR = 9;
enum { B_UP = 1, B_DOWN = 2, B_HASH = 4, B_STAR = 8 };
uint8_t nowButtons = 0, prevButtons = 0;

void readButtons() {
  prevButtons = nowButtons;
  nowButtons = (digitalRead(PIN_UP)   == LOW ? B_UP   : 0) |
               (digitalRead(PIN_DOWN) == LOW ? B_DOWN : 0) |
               (digitalRead(PIN_HASH) == LOW ? B_HASH : 0) |
               (digitalRead(PIN_STAR) == LOW ? B_STAR : 0);
}
bool pressed(uint8_t b) { return (nowButtons & b) && !(prevButtons & b); }

// ---- Layout ------------------------------------------------------------------
// Three shelves; each value is the y of the shelf line (sprites stand on it)
const uint8_t SHELF_Y[3] = {26, 44, 62};
const uint8_t CAT_X = 4;
// Pounce animation: how far forward the cat leaps on each frame
const uint8_t POUNCE_OFS[] = {4, 9, 13, 16, 16, 13, 9, 5, 2};
const uint8_t POUNCE_LEN = sizeof(POUNCE_OFS);

// ---- Game state ----------------------------------------------------------------
enum ObjType : uint8_t { NONE, MOUSE, DOG, FISH };
struct Obj { ObjType type; uint8_t lane; int16_t x; };  // x is fixed point (px * 16)
const uint8_t MAX_OBJ = 7;
Obj objs[MAX_OBJ];

uint8_t catLane;
uint8_t pounceT;        // 0 = sitting, else frames into the pounce
uint8_t hurtT;          // invulnerability frames after a hit
uint8_t hearts;
uint16_t score, best;
bool newBest;
uint8_t spawnT;
uint16_t speedQ;        // object speed in 1/16 px per frame
uint8_t frame;

// One floating "+1" / "+3" / "ouch" message
int8_t popX, popY; uint8_t popT; const char* popText;

enum State { TITLE, PLAY, PAUSED, GAMEOVER };
State state = TITLE;

// ---- EEPROM best score (separate slot from Cave Flyer) ---------------------
const int EE_MAGIC_ADDR = 16, EE_BEST_ADDR = 17;
const uint8_t EE_MAGIC = 0xCA;

void loadBest() {
  if (EEPROM.read(EE_MAGIC_ADDR) == EE_MAGIC) EEPROM.get(EE_BEST_ADDR, best);
  else best = 0;
}
void saveBest() {
  EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
  EEPROM.put(EE_BEST_ADDR, best);
}

// ---- Helpers -------------------------------------------------------------------
uint8_t objW(uint8_t t) { return t == DOG ? DOG_W : (t == MOUSE ? MOUSE_W : FISH_W); }
uint8_t objH(uint8_t t) { return t == DOG ? DOG_H : (t == MOUSE ? MOUSE_H : FISH_H); }

void popup(int8_t x, int8_t y, const char* text) {
  popX = x; popY = y; popT = 20; popText = text;
}

void loseHeart(int8_t x, int8_t y) {
  if (hurtT) return;
  hearts--;
  hurtT = 45;
  popup(x, y, "ouch");
  if (hearts == 0) {
    if (score > best) { best = score; newBest = true; saveBest(); }
    state = GAMEOVER;
  }
}

void newGame() {
  randomSeed(micros());
  for (uint8_t i = 0; i < MAX_OBJ; i++) objs[i].type = NONE;
  catLane = 1; pounceT = 0; hurtT = 0;
  hearts = 3; score = 0; newBest = false;
  spawnT = 30; speedQ = 14; popT = 0;
  state = PLAY;
}

void spawn() {
  uint8_t slot = MAX_OBJ;
  for (uint8_t i = 0; i < MAX_OBJ; i++) if (objs[i].type == NONE) { slot = i; break; }
  if (slot == MAX_OBJ) return;

  uint8_t lane = random(3);
  // Don't stack a new object right on top of one just spawned in the same lane
  for (uint8_t i = 0; i < MAX_OBJ; i++)
    if (objs[i].type != NONE && objs[i].lane == lane && objs[i].x > 100 * 16) return;

  uint8_t r = random(100);
  ObjType t = r < 62 ? MOUSE : (r < 88 ? DOG : FISH);

  if (t == DOG) {
    // Always leave an escape route: only one dog near the right side at a time
    for (uint8_t i = 0; i < MAX_OBJ; i++)
      if (objs[i].type == DOG && objs[i].x > 70 * 16) { t = MOUSE; break; }
  }
  objs[slot].type = t;
  objs[slot].lane = lane;
  objs[slot].x = 128 * 16;
}

// ---- Update --------------------------------------------------------------------
void updatePlay() {
  if (pressed(B_STAR)) { state = PAUSED; return; }

  // Move between shelves (not mid-pounce)
  if (!pounceT) {
    if (pressed(B_UP) && catLane > 0) catLane--;
    if (pressed(B_DOWN) && catLane < 2) catLane++;
    if (pressed(B_HASH)) pounceT = 1;
  } else if (++pounceT > POUNCE_LEN) {
    pounceT = 0;
  }
  if (hurtT) hurtT--;
  if (popT) { popT--; if (popT % 3 == 0) popY--; }

  // Difficulty ramps with score
  speedQ = min(44, 14 + score / 2);
  uint8_t interval = max(16, 55 - (int)score);
  if (spawnT) spawnT--;
  else { spawn(); spawnT = interval + random(interval / 2); }

  // Cat hit box. Pouncing paws reach further forward.
  uint8_t ofs = pounceT ? POUNCE_OFS[pounceT - 1] : 0;
  int16_t catL = CAT_X + ofs + 2;
  int16_t catR = CAT_X + ofs + CAT_SIT_W - 1 + (pounceT ? 2 : 0);

  for (uint8_t i = 0; i < MAX_OBJ; i++) {
    Obj& o = objs[i];
    if (o.type == NONE) continue;
    // Mice scurry a little faster than the rest
    o.x -= (o.type == MOUSE) ? speedQ + speedQ / 4 : speedQ;
    int16_t ox = o.x >> 4;
    int8_t oy = SHELF_Y[o.lane] - objH(o.type);

    if (ox + objW(o.type) < 0) {           // got past the cat
      if (o.type == MOUSE) loseHeart(2, oy);
      o.type = NONE;
      continue;
    }
    if (o.lane != catLane) continue;
    bool touching = ox <= catR && ox + objW(o.type) - 1 >= catL;
    if (!touching) continue;

    if (o.type == MOUSE && pounceT) {
      score++;
      popup(ox, oy - 2, "+1");
      o.type = NONE;
    } else if (o.type == FISH) {
      score += 3;
      if (hearts < 3) hearts++;
      popup(ox, oy - 2, "+3");
      o.type = NONE;
    } else if (o.type == DOG) {
      loseHeart(CAT_X + 4, SHELF_Y[catLane] - 14);
    }
    // A mouse touching a sitting cat just runs on by - you have to pounce!
  }
}

// ---- Drawing -------------------------------------------------------------------
void drawCentered(const char* s, uint8_t y) {
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
}

void drawShelves() {
  for (uint8_t l = 0; l < 3; l++) {
    u8g2.drawHLine(0, SHELF_Y[l], 128);
    // little brackets under the shelf boards
    if (l < 2) for (uint8_t x = 20; x < 128; x += 50) u8g2.drawPixel(x, SHELF_Y[l] + 1);
  }
}

void drawHud() {
  for (uint8_t i = 0; i < hearts; i++) u8g2.drawXBMP(1 + i * 9, 1, HEART_W, HEART_H, heart_bits);
  char buf[8];
  utoa(score, buf, 10);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(127 - u8g2.getStrWidth(buf), 7, buf);
}

void drawCat(uint8_t x, uint8_t lane, bool pouncing) {
  uint8_t y = SHELF_Y[lane] - CAT_SIT_H;
  u8g2.drawXBMP(x, y, CAT_SIT_W, CAT_SIT_H, pouncing ? cat_pounce_bits : cat_sit_bits);
}

void drawWorld() {
  drawShelves();
  for (uint8_t i = 0; i < MAX_OBJ; i++) {
    Obj& o = objs[i];
    if (o.type == NONE) continue;
    int16_t x = o.x >> 4;
    int8_t y = SHELF_Y[o.lane] - objH(o.type);
    if (o.type == MOUSE) {
      // bob up and down a pixel so it looks like it's running
      u8g2.drawXBMP(x, y - ((frame / 3 + i) & 1), MOUSE_W, MOUSE_H, mouse_bits);
    } else if (o.type == DOG) {
      u8g2.drawXBMP(x, y, DOG_W, DOG_H, dog_bits);
    } else {
      // fish flop around
      u8g2.drawXBMP(x, y - ((frame / 4) & 1) * 2, FISH_W, FISH_H, fish_bits);
    }
  }
  // Cat blinks while invulnerable after a hit
  if (!hurtT || (frame / 3) % 2) {
    uint8_t ofs = pounceT ? POUNCE_OFS[pounceT - 1] : 0;
    drawCat(CAT_X + ofs, catLane, pounceT != 0);
  }
  if (popT) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(constrain(popX, 0, 110), max((int)popY, 8), popText);
  }
  drawHud();
}

void drawTitle() {
  u8g2.setFont(u8g2_font_ncenB10_tr);
  drawCentered("KITTY POUNCE", 14);
  // little scene: cat stalking a mouse
  u8g2.drawHLine(0, 33, 128);
  uint8_t cx = 30 + ((frame / 20) % 2) * 6;
  u8g2.drawXBMP(cx, 33 - CAT_SIT_H, 16, 12, (frame / 20) % 2 ? cat_pounce_bits : cat_sit_bits);
  u8g2.drawXBMP(80 - (frame / 2) % 8, 33 - MOUSE_H - ((frame / 3) & 1), MOUSE_W, MOUSE_H, mouse_bits);
  u8g2.drawXBMP(104, 33 - FISH_H, FISH_W, FISH_H, fish_bits);
  u8g2.setFont(u8g2_font_5x7_tr);
  drawCentered("^v shelf  # pounce", 43);
  char buf[16];
  snprintf(buf, sizeof(buf), "best %u", best);
  drawCentered(buf, 52);
  if ((millis() / 500) % 2) drawCentered("press # to start", 61);
}

void drawGameOver() {
  u8g2.setDrawColor(0);
  u8g2.drawBox(14, 10, 100, 46);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(14, 10, 100, 46);
  u8g2.setFont(u8g2_font_ncenB10_tr);
  drawCentered("GAME OVER", 25);
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[20];
  snprintf(buf, sizeof(buf), "score %u", score);
  drawCentered(buf, 35);
  if (newBest) drawCentered("NEW BEST!", 44);
  else { snprintf(buf, sizeof(buf), "best %u", best); drawCentered(buf, 44); }
  if ((millis() / 500) % 2) drawCentered("# to retry", 53);
}

// ---- Main --------------------------------------------------------------------
void setup() {
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_HASH, INPUT_PULLUP);
  pinMode(PIN_STAR, INPUT_PULLUP);

  u8g2.setBusClock(400000);
  u8g2.begin();

  loadBest();
  if (digitalRead(PIN_DOWN) == LOW) { best = 0; saveBest(); }  // hold v at power-on to reset
  readButtons();
}

void loop() {
  static uint32_t nextFrame = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextFrame) < 0) return;
  nextFrame = now + 33;  // ~30 fps
  frame++;

  readButtons();
  u8g2.clearBuffer();

  switch (state) {
    case TITLE:
      if (pressed(B_HASH)) newGame();
      drawTitle();
      break;
    case PLAY:
      updatePlay();
      drawWorld();
      break;
    case PAUSED:
      if (pressed(B_STAR)) state = PLAY;
      drawWorld();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.setDrawColor(0);
      u8g2.drawBox(30, 22, 68, 20);
      u8g2.setDrawColor(1);
      u8g2.drawFrame(30, 22, 68, 20);
      drawCentered("PAUSED", 37);
      break;
    case GAMEOVER:
      drawWorld();
      drawGameOver();
      if (pressed(B_HASH)) newGame();
      break;
  }
  u8g2.sendBuffer();
}
