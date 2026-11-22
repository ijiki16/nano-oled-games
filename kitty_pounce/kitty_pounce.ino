/*
  KITTY POUNCE - for Arduino Nano + 1.3" SSD1306 OLED with 4 buttons

  A cat guards three shelves. Mice run in from the right: pounce on them
  before they get past! Dogs charge in too: get out of their shelf.
  Fish are a treat: grab one to win back a heart.
  A dog barks "!" at the right edge a second before it charges in.
  Catnip (rare): 5 seconds of double points and quicker pounces.

  Controls
    ^ / v    jump up / down a shelf
    #        pounce (start / retry on menus)
    *        pause / resume (on the title screen: sound on/off)

  Rules
    +1 point per mouse caught, +3 per fish
    lose a heart when a mouse escapes or a dog hits you
    3 hearts; game over at 0
    Hold v while powering on to reset the best score.

  Wiring
    OLED SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
    K1 (^) -> D12, K2 (v) -> D11, K3 (#) -> D10, K4 (*) -> D9
    Buzzer (optional): D5 -> 100 ohm -> passive piezo -> GND
    PowerBoost LBO (optional): D3, shows a low-battery icon

  The screen turns off after 1 minute without keys on the title, pause
  and game-over screens; any key wakes it.
  Needs the NanoOledKit library from this repo's libraries/ folder.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <NanoOledKit.h>
#include "sprites.h"

// Page mode: draws the screen in 4 strips, saving 768 bytes of RAM.
U8G2_SSD1306_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const uint32_t IDLE_SLEEP_MS = 60000UL;

// ---- Sounds ------------------------------------------------------------------
const Note SND_START[] PROGMEM  = {{988, 60}, {1319, 90}, {0, 0}};
const Note SND_POUNCE[] PROGMEM = {{1400, 20}, {1000, 30}, {0, 0}};
const Note SND_CATCH[] PROGMEM  = {{1760, 40}, {2349, 60}, {0, 0}};
const Note SND_FISH[] PROGMEM   = {{1319, 60}, {1568, 60}, {2093, 90}, {0, 0}};
const Note SND_BARK[] PROGMEM   = {{180, 70}, {0, 50}, {160, 90}, {0, 0}};
const Note SND_OUCH[] PROGMEM   = {{400, 80}, {250, 120}, {0, 0}};
const Note SND_CATNIP[] PROGMEM = {{1568, 50}, {2093, 50}, {2637, 50}, {3136, 90}, {0, 0}};
const Note SND_OVER[] PROGMEM   = {{784, 150}, {659, 150}, {523, 150}, {392, 300}, {0, 0}};
const Note SND_BLIP[] PROGMEM   = {{1500, 40}, {0, 0}};

// ---- Layout ------------------------------------------------------------------
// Three shelves; each value is the y of the shelf line (sprites stand on it)
const uint8_t SHELF_Y[3] = {26, 44, 62};
const uint8_t CAT_X = 4;
// Pounce animation: how far forward the cat leaps on each frame
const uint8_t POUNCE_OFS[] = {4, 9, 13, 16, 16, 13, 9, 5, 2};
const uint8_t POUNCE_LEN = sizeof(POUNCE_OFS);

// ---- Game state ----------------------------------------------------------------
enum ObjType : uint8_t { NONE, MOUSE, DOG, FISH, CATNIP };
struct Obj { ObjType type; uint8_t lane; uint8_t warn; int16_t x; };  // x is fixed point (px * 16)
const uint8_t DOG_WARN = 30;      // frames a dog barks "!" before it charges in
const uint8_t CATNIP_TIME = 150;  // 5 seconds of double points
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
uint8_t catnipT;        // frames of catnip power left

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
uint8_t objW(uint8_t t) { return t == DOG ? DOG_W : t == MOUSE ? MOUSE_W : t == FISH ? FISH_W : CATNIP_W; }
uint8_t objH(uint8_t t) { return t == DOG ? DOG_H : t == MOUSE ? MOUSE_H : t == FISH ? FISH_H : CATNIP_H; }

void popup(int8_t x, int8_t y, const char* text) {
  popX = x; popY = y; popT = 20; popText = text;
}

void loseHeart(int8_t x, int8_t y) {
  if (hurtT) return;
  hearts--;
  hurtT = 45;
  popup(x, y, "ouch");
  sfxPlay(SND_OUCH);
  if (hearts == 0) {
    if (score > best) { best = score; newBest = true; saveBest(); }
    state = GAMEOVER;
    sfxPlay(SND_OVER);
  }
}

void newGame() {
  randomSeed(micros());
  for (uint8_t i = 0; i < MAX_OBJ; i++) objs[i].type = NONE;
  catLane = 1; pounceT = 0; hurtT = 0;
  hearts = 3; score = 0; newBest = false;
  spawnT = 30; speedQ = 14; popT = 0; catnipT = 0;
  state = PLAY;
  sfxPlay(SND_START);
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
  ObjType t = r < 62 ? MOUSE : r < 87 ? DOG : r < 97 ? FISH : CATNIP;
  if (t == CATNIP && catnipT) t = FISH;

  if (t == DOG) {
    // Always leave an escape route: only one dog near the right side at a time
    for (uint8_t i = 0; i < MAX_OBJ; i++)
      if (objs[i].type == DOG && objs[i].x > 70 * 16) { t = MOUSE; break; }
  }
  objs[slot].type = t;
  objs[slot].lane = lane;
  objs[slot].x = 128 * 16;
  objs[slot].warn = 0;
  if (t == DOG) { objs[slot].warn = DOG_WARN; sfxPlay(SND_BARK); }
}

// ---- Update --------------------------------------------------------------------
void updatePlay() {
  if (pressed(B_STAR)) { state = PAUSED; return; }

  // Move between shelves (not mid-pounce)
  if (!pounceT) {
    if (pressed(B_UP) && catLane > 0) catLane--;
    if (pressed(B_DOWN) && catLane < 2) catLane++;
    if (pressed(B_HASH)) { pounceT = 1; sfxPlay(SND_POUNCE); }
  } else {
    pounceT += (catnipT && pounceT < POUNCE_LEN) ? 2 : 1;  // catnip: quicker pounce
    if (pounceT > POUNCE_LEN) pounceT = 0;
  }
  if (hurtT) hurtT--;
  if (catnipT) catnipT--;
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
    if (o.warn) { o.warn--; continue; }   // dog barking at the edge, not in yet
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
      score += catnipT ? 2 : 1;
      popup(ox, oy - 2, catnipT ? "+2" : "+1");
      sfxPlay(SND_CATCH);
      o.type = NONE;
    } else if (o.type == FISH) {
      score += 3;
      if (hearts < 3) hearts++;
      popup(ox, oy - 2, "+3");
      sfxPlay(SND_FISH);
      o.type = NONE;
    } else if (o.type == CATNIP) {
      catnipT = CATNIP_TIME;
      popup(ox, oy - 2, "x2!");
      sfxPlay(SND_CATNIP);
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
  // catnip time left: a shrinking bar under the hearts' right side
  if (catnipT) {
    u8g2.drawStr(30, 7, "x2");
    u8g2.drawBox(42, 3, catnipT / 6, 2);
  }
  if (lowBattery() && (millis() / 500) % 2) drawLowBattery(u8g2, 94, 2);
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
    if (o.warn) {  // the dog is coming: a blinking "!" at the shelf's right edge
      if ((o.warn / 4) % 2) {
        u8g2.setFont(u8g2_font_ncenB10_tr);
        u8g2.drawStr(121, SHELF_Y[o.lane] - 2, "!");
      }
    } else if (o.type == MOUSE) {
      // bob up and down a pixel so it looks like it's running
      u8g2.drawXBMP(x, y - ((frame / 3 + i) & 1), MOUSE_W, MOUSE_H, mouse_bits);
    } else if (o.type == DOG) {
      u8g2.drawXBMP(x, y, DOG_W, DOG_H, dog_bits);
    } else if (o.type == CATNIP) {
      // catnip sparkles
      u8g2.drawXBMP(x, y, CATNIP_W, CATNIP_H, catnip_bits);
      if ((frame / 4) & 1) u8g2.drawPixel(x + 9, y + ((frame / 8) & 3));
    } else {
      // fish flop around
      u8g2.drawXBMP(x, y - ((frame / 4) & 1) * 2, FISH_W, FISH_H, fish_bits);
    }
  }
  // Cat blinks while invulnerable after a hit
  if (!hurtT || (frame / 3) % 2) {
    uint8_t ofs = pounceT ? POUNCE_OFS[pounceT - 1] : 0;
    drawCat(CAT_X + ofs, catLane, pounceT != 0);
    if (catnipT && (frame / 2) % 2)  // catnip sparkle over the cat's head
      u8g2.drawPixel(CAT_X + ofs + 3 + (frame % 9), SHELF_Y[catLane] - CAT_SIT_H - 2);
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
  sfxDrawIcon(u8g2, 120, 56);  // sound on/off, toggled with *
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

void drawPaused() {
  drawWorld();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.setDrawColor(0);
  u8g2.drawBox(30, 22, 68, 20);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(30, 22, 68, 20);
  drawCentered("PAUSED", 37);
}

// ---- Main --------------------------------------------------------------------
void setup() {
  keysBegin();
  sfxBegin();
  powerBegin();

  u8g2.setBusClock(400000);
  u8g2.begin();

  loadBest();
  if (held(B_DOWN)) { best = 0; saveBest(); }  // hold v at power-on to reset
}

void update() {
  switch (state) {
    case TITLE:
      if (pressed(B_HASH)) newGame();
      else if (pressed(B_STAR)) { sfxSetMuted(!sfxMuted()); sfxPlay(SND_BLIP); }
      else powerIdle(u8g2, IDLE_SLEEP_MS);
      break;
    case PLAY:
      updatePlay();
      break;
    case PAUSED:
      if (pressed(B_STAR)) state = PLAY;
      else powerIdle(u8g2, IDLE_SLEEP_MS);
      break;
    case GAMEOVER:
      if (pressed(B_HASH)) newGame();
      else powerIdle(u8g2, IDLE_SLEEP_MS);
      break;
  }
}

// In page mode the draw code runs once per strip, so it must not change game state.
void draw() {
  switch (state) {
    case TITLE:    drawTitle(); break;
    case PLAY:     drawWorld(); break;
    case PAUSED:   drawPaused(); break;
    case GAMEOVER: drawWorld(); drawGameOver(); break;
  }
}

void loop() {
  static uint32_t nextFrame = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextFrame) < 0) return;
  nextFrame = now + 33;  // ~30 fps
  frame++;

  readKeys();
  sfxUpdate();
  update();

  u8g2.firstPage();
  do { draw(); } while (u8g2.nextPage());
}
