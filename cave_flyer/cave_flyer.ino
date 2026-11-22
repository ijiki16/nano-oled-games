/*
  CAVE FLYER - for Arduino Nano + 1.3" SSD1306 OLED with 4 buttons

  Fly through an endless scrolling cave. Hold ^ or # to climb,
  let go to fall. Avoid the walls and the floating rocks, grab the
  coins (+10). Every 100 metres the level goes up: the cave narrows
  and speeds up.

  Controls
    ^ or #   hold to fly up
    *        pause / resume (on the title screen: sound on/off)
    #        start / retry

  Wiring
    OLED SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
    K1 (^) -> D12, K2 (v) -> D11, K3 (#) -> D10, K4 (*) -> D9
    Buzzer (optional): D5 -> 100 ohm -> passive piezo -> GND
    PowerBoost LBO (optional): D3, shows a low-battery icon

  The screen turns off after 1 minute without keys on the title, pause
  and game-over screens; any key wakes it.
  Best score is saved in EEPROM and survives power-off.
  Hold v while powering on to reset the best score.

  Needs the NanoOledKit library from this repo's libraries/ folder.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <NanoOledKit.h>

// Page mode: draws the screen in 4 strips, saving 768 bytes of RAM.
U8G2_SSD1306_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const uint32_t IDLE_SLEEP_MS = 60000UL;

// ---- Sounds ------------------------------------------------------------------
const Note SND_START[] PROGMEM = {{880, 50}, {1320, 80}, {0, 0}};
const Note SND_THRUST[] PROGMEM = {{110, 12}, {0, 0}};
const Note SND_COIN[] PROGMEM = {{1800, 40}, {2400, 70}, {0, 0}};
const Note SND_LEVEL[] PROGMEM = {{660, 80}, {880, 80}, {1320, 140}, {0, 0}};
const Note SND_CRASH[] PROGMEM = {{220, 60}, {160, 60}, {110, 90}, {70, 160}, {0, 0}};
const Note SND_BEST[] PROGMEM = {{1047, 100}, {1319, 100}, {1568, 100}, {2093, 220}, {0, 0}};
const Note SND_BLIP[] PROGMEM = {{1500, 40}, {0, 0}};

// ---- Cave ------------------------------------------------------------------
// The cave is a ring buffer of 4-pixel-wide columns. Each has a ceiling
// height, a floor height, an optional rock and an optional coin.
const uint8_t SEG_W = 4;
const uint8_t NSEG  = 128 / SEG_W + 2;  // 34: one extra on each side for scrolling
const uint8_t ROCK_H = 6;
const uint8_t COIN_H = 4;

uint8_t caveTop[NSEG];
uint8_t caveBot[NSEG];
int8_t  rockY[NSEG];   // -1 = no rock
int8_t  coinY[NSEG];   // -1 = no coin

uint8_t head;          // index of the left-most column on screen
uint8_t scrollPx;      // 0..SEG_W-1, sub-column scroll offset
int8_t  center;        // centre line of the cave being generated
int8_t  slope;         // how fast the centre drifts
uint8_t gap;           // current cave height
uint8_t sinceRock;     // columns since the last rock
uint8_t safeCols;      // rock-free columns left at the start

void genColumn(uint8_t idx) {
  // Drift the centre line, occasionally changing direction
  if (random(10) == 0) slope = random(-2, 3);
  center += slope;
  int8_t half = gap / 2;
  int8_t lo = half + 1, hi = 63 - half - 1;
  if (center < lo) { center = lo; slope = 1; }
  if (center > hi) { center = hi; slope = -1; }

  caveTop[idx] = center - half;
  caveBot[idx] = center + half;
  rockY[idx] = -1;
  coinY[idx] = -1;

  if (safeCols) { safeCols--; sinceRock = 0; return; }
  sinceRock++;
  // Rocks get more frequent as the game goes on, but never back to back
  if (sinceRock > 10 && random(100) < 10) {
    uint8_t space = caveBot[idx] - caveTop[idx];
    if (space > ROCK_H + 16) {  // always leave room to fly past
      rockY[idx] = caveTop[idx] + 8 + random(space - ROCK_H - 16 + 1);
      sinceRock = 0;
      return;
    }
  }
  // Coins float in the middle of the cave, away from rocks
  if (sinceRock > 3 && random(100) < 5) coinY[idx] = center - COIN_H / 2;
}

// ---- Ship & game state -------------------------------------------------------
const uint8_t SHIP_X = 24;
const uint8_t SHIP_W = 9;
const uint8_t SHIP_H = 5;

int16_t  shipY;      // fixed point, 4 fractional bits (y * 16)
int16_t  shipV;      // velocity, same units per frame
uint16_t speedQ;     // scroll speed in 1/16 px per frame
uint16_t scrollAcc;
uint32_t distance;   // pixels travelled
uint16_t bonus;      // points from coins
uint8_t  level;
uint8_t  levelFlash; // frames left to show "LEVEL n"
uint16_t best;
bool     newBest;
bool     thrusting;
uint8_t  frame;

enum State { TITLE, PLAY, PAUSED, CRASH, GAMEOVER };
State state = TITLE;
uint8_t crashFrame;

uint16_t metres() { return distance / 8; }
uint16_t score() { return metres() + bonus; }

// ---- EEPROM best score -----------------------------------------------------
const int EE_MAGIC_ADDR = 0, EE_BEST_ADDR = 1;
const uint8_t EE_MAGIC = 0xCF;

void loadBest() {
  if (EEPROM.read(EE_MAGIC_ADDR) == EE_MAGIC) EEPROM.get(EE_BEST_ADDR, best);
  else best = 0;
}
void saveBest() {
  EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
  EEPROM.put(EE_BEST_ADDR, best);
}

// ---- Game flow ---------------------------------------------------------------
void newGame() {
  randomSeed(micros());
  gap = 46; center = 32; slope = 0;
  sinceRock = 0; safeCols = NSEG + 8;
  head = 0; scrollPx = 0;
  for (uint8_t i = 0; i < NSEG; i++) genColumn(i);
  shipY = 30 * 16; shipV = 0;
  speedQ = 20; scrollAcc = 0;
  distance = 0; bonus = 0; newBest = false;
  level = 1; levelFlash = 0;
  state = PLAY;
  sfxPlay(SND_START);
}

// Columns under the ship (a 1px margin keeps it fair)
uint8_t shipCol0() { return (SHIP_X + 1 + scrollPx) / SEG_W; }
uint8_t shipCol1() { return (SHIP_X + SHIP_W - 2 + scrollPx) / SEG_W; }

bool collides() {
  int8_t y = shipY >> 4;
  if (y < 0 || y + SHIP_H > 64) return true;
  for (uint8_t c = shipCol0(); c <= shipCol1(); c++) {
    uint8_t i = (head + c) % NSEG;
    if (y + 1 < caveTop[i] || y + SHIP_H - 1 > caveBot[i]) return true;
    if (rockY[i] >= 0 && y + SHIP_H - 1 > rockY[i] && y + 1 < rockY[i] + ROCK_H) return true;
  }
  return false;
}

void collectCoins() {
  int8_t y = shipY >> 4;
  for (uint8_t c = shipCol0(); c <= shipCol1() + 1; c++) {
    uint8_t i = (head + c) % NSEG;
    if (coinY[i] >= 0 && y + SHIP_H > coinY[i] && y < coinY[i] + COIN_H) {
      coinY[i] = -1;
      bonus += 10;
      sfxPlay(SND_COIN);
    }
  }
}

void updatePlay() {
  if (pressed(B_STAR)) { state = PAUSED; return; }

  // Physics
  thrusting = held(B_UP | B_HASH);
  shipV += thrusting ? -5 : 3;
  shipV = constrain(shipV, -36, 40);
  shipY += shipV;
  if (thrusting && (frame & 3) == 0) sfxPlaySoft(SND_THRUST);

  // Scroll the cave
  scrollAcc += speedQ;
  while (scrollAcc >= 16) {
    scrollAcc -= 16;
    distance++;
    if (++scrollPx == SEG_W) {
      scrollPx = 0;
      genColumn(head);              // old left column becomes the new right one
      head = (head + 1) % NSEG;
    }
  }

  // Level up every 100 m: narrower and faster
  uint8_t lv = 1 + metres() / 100;
  if (lv != level) { level = lv; levelFlash = 45; sfxPlay(SND_LEVEL); }
  if (levelFlash) levelFlash--;
  gap = max(22, 46 - (level - 1) * 3);
  speedQ = min(44, 20 + (level - 1) * 3);

  collectCoins();
  if (collides()) {
    state = CRASH;
    crashFrame = 0;
    uint16_t s = score();
    if (s > best) { best = s; newBest = true; saveBest(); }
    sfxPlay(SND_CRASH);
  }
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
    case CRASH:
      if (++crashFrame > 12) {
        state = GAMEOVER;
        if (newBest) sfxPlay(SND_BEST);
      }
      break;
    case GAMEOVER:
      if (pressed(B_HASH)) newGame();
      else powerIdle(u8g2, IDLE_SLEEP_MS);
      break;
  }
}

// ---- Drawing -----------------------------------------------------------------
// In page mode the draw code runs once per strip, so it must not change game state.
void drawCave() {
  for (uint8_t c = 0; c < NSEG; c++) {
    uint8_t i = (head + c) % NSEG;
    int16_t x = c * SEG_W - scrollPx;
    if (x >= 128) break;
    u8g2.drawBox(x, 0, SEG_W, caveTop[i]);
    u8g2.drawBox(x, caveBot[i], SEG_W, 64 - caveBot[i]);
    if (rockY[i] >= 0) u8g2.drawBox(x, rockY[i], SEG_W, ROCK_H);
    if (coinY[i] >= 0) {  // small diamond
      int8_t y = coinY[i];
      u8g2.drawHLine(x + 1, y, 2);
      u8g2.drawHLine(x, y + 1, 4);
      u8g2.drawHLine(x, y + 2, 4);
      u8g2.drawHLine(x + 1, y + 3, 2);
    }
  }
}

void drawShip(int8_t y) {
  //   ##
  //  ######>
  //  ########
  //  ######>
  //   ##
  u8g2.drawBox(SHIP_X + 1, y + 1, 5, 3);
  u8g2.drawTriangle(SHIP_X + 5, y, SHIP_X + 5, y + 5, SHIP_X + 9, y + 2);
  u8g2.drawPixel(SHIP_X + 2, y);
  u8g2.drawPixel(SHIP_X + 2, y + 4);
  if (thrusting && (millis() / 60) % 2) {  // flickering exhaust
    u8g2.drawHLine(SHIP_X - 3, y + 2, 3);
    u8g2.drawPixel(SHIP_X - 1, y + 1);
    u8g2.drawPixel(SHIP_X - 1, y + 3);
  }
}

void drawScore() {
  char buf[8];
  utoa(score(), buf, 10);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setFontMode(1);
  u8g2.setDrawColor(2);           // XOR: readable over rock and over empty space
  u8g2.drawStr(2, 8, buf);
  if (lowBattery() && (millis() / 500) % 2) drawLowBattery(u8g2, 116, 2);
  u8g2.setDrawColor(1);
}

void drawCentered(const char* s, uint8_t y) {
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
}

void drawBanner(const char* s) {
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.setDrawColor(0);
  u8g2.drawBox(30, 22, 68, 20);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(30, 22, 68, 20);
  drawCentered(s, 37);
}

void drawTitle() {
  // Decorative cave edges
  for (uint8_t x = 0; x < 128; x += 4) {
    uint8_t t = 3 + ((x * 7) % 11) / 2;
    uint8_t b = 3 + ((x * 5) % 13) / 2;
    u8g2.drawBox(x, 0, 4, t);
    u8g2.drawBox(x, 64 - b, 4, b);
  }
  u8g2.setFont(u8g2_font_ncenB12_tr);
  drawCentered("CAVE FLYER", 26);
  u8g2.setFont(u8g2_font_5x7_tr);
  drawCentered("hold ^ or # to fly", 37);
  char buf[16];
  snprintf(buf, sizeof(buf), "best %u", best);
  drawCentered(buf, 46);
  if ((millis() / 500) % 2) drawCentered("press # to start", 55);
  // sound on/off, toggled with *
  u8g2.setDrawColor(0);
  u8g2.drawBox(116, 0, 12, 10);
  u8g2.setDrawColor(1);
  sfxDrawIcon(u8g2, 119, 1);
}

void drawGameOver() {
  u8g2.setDrawColor(0);
  u8g2.drawBox(14, 10, 100, 44);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(14, 10, 100, 44);
  u8g2.setFont(u8g2_font_ncenB10_tr);
  drawCentered("CRASHED!", 25);
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[20];
  snprintf(buf, sizeof(buf), "score %u  lv %u", score(), level);
  drawCentered(buf, 35);
  if (newBest) drawCentered("NEW BEST!", 44);
  else { snprintf(buf, sizeof(buf), "best %u", best); drawCentered(buf, 44); }
  if ((millis() / 500) % 2) drawCentered("# to retry", 52);
}

void draw() {
  switch (state) {
    case TITLE:
      drawTitle();
      break;
    case PLAY:
      drawCave();
      drawShip(shipY >> 4);
      drawScore();
      if (levelFlash && (levelFlash / 6) % 2 == 0) {
        char buf[10];
        snprintf(buf, sizeof(buf), "LEVEL %u", level);
        drawBanner(buf);
      }
      break;
    case PAUSED:
      drawCave();
      drawShip(shipY >> 4);
      drawScore();
      drawBanner("PAUSED");
      break;
    case CRASH: {  // short explosion animation
      drawCave();
      int8_t y = (shipY >> 4) + 2;
      uint8_t r = 2 + crashFrame * 2;
      u8g2.drawCircle(SHIP_X + 4, y, r);
      if (r > 4) u8g2.drawCircle(SHIP_X + 4, y, r - 4);
      randomSeed(crashFrame + 1);  // same debris in every page strip
      for (uint8_t k = 0; k < 6; k++)
        u8g2.drawPixel(SHIP_X + 4 + random(-r, r + 1), y + random(-r, r + 1));
      drawScore();
      break;
    }
    case GAMEOVER:
      drawCave();
      drawGameOver();
      break;
  }
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
