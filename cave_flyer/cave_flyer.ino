/*
  CAVE FLYER - for Arduino Nano + 1.3" SSD1306 OLED with 4 buttons

  Fly through an endless scrolling cave. Hold ^ or # to climb,
  let go to fall. Avoid the walls and the floating rocks.
  The cave narrows and speeds up the further you get.

  Controls
    ^ or #   hold to fly up
    *        pause / resume
    #        start / retry

  Wiring
    OLED SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
    K1 (^) -> D12, K2 (v) -> D11, K3 (#) -> D10, K4 (*) -> D9

  Best score is saved in EEPROM and survives power-off.
  Hold v while powering on to reset the best score.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---- Buttons ---------------------------------------------------------------
const uint8_t PIN_UP = 12, PIN_DOWN = 11, PIN_HASH = 10, PIN_STAR = 9;

bool held(uint8_t pin) { return digitalRead(pin) == LOW; }

// Edge detection for "pressed this frame"
uint8_t prevButtons = 0;
uint8_t nowButtons  = 0;
enum { B_UP = 1, B_DOWN = 2, B_HASH = 4, B_STAR = 8 };

void readButtons() {
  prevButtons = nowButtons;
  nowButtons = (held(PIN_UP) ? B_UP : 0) | (held(PIN_DOWN) ? B_DOWN : 0) |
               (held(PIN_HASH) ? B_HASH : 0) | (held(PIN_STAR) ? B_STAR : 0);
}
bool pressed(uint8_t b) { return (nowButtons & b) && !(prevButtons & b); }

// ---- Cave ------------------------------------------------------------------
// The cave is a ring buffer of 4-pixel-wide columns. Each has a ceiling
// height, a floor height and an optional rock.
const uint8_t SEG_W = 4;
const uint8_t NSEG  = 128 / SEG_W + 2;  // 34: one extra on each side for scrolling
const uint8_t ROCK_H = 6;

uint8_t caveTop[NSEG];
uint8_t caveBot[NSEG];
int8_t  rockY[NSEG];   // -1 = no rock

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

  if (safeCols) { safeCols--; sinceRock = 0; return; }
  sinceRock++;
  // Rocks get more frequent as the game goes on, but never back to back
  if (sinceRock > 10 && random(100) < 10) {
    uint8_t space = caveBot[idx] - caveTop[idx];
    if (space > ROCK_H + 16) {  // always leave room to fly past
      rockY[idx] = caveTop[idx] + 8 + random(space - ROCK_H - 16 + 1);
      sinceRock = 0;
    }
  }
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
uint16_t best;
bool     newBest;
bool     thrusting;

enum State { TITLE, PLAY, PAUSED, CRASH, GAMEOVER };
State state = TITLE;
uint8_t crashFrame;

uint16_t score() { return distance / 8; }

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
  distance = 0; newBest = false;
  state = PLAY;
}

bool collides() {
  int8_t y = shipY >> 4;
  if (y < 0 || y + SHIP_H > 64) return true;
  // Check every column the ship overlaps (a 1px margin keeps it fair)
  uint8_t c0 = (SHIP_X + 1 + scrollPx) / SEG_W;
  uint8_t c1 = (SHIP_X + SHIP_W - 2 + scrollPx) / SEG_W;
  for (uint8_t c = c0; c <= c1; c++) {
    uint8_t i = (head + c) % NSEG;
    if (y + 1 < caveTop[i] || y + SHIP_H - 1 > caveBot[i]) return true;
    if (rockY[i] >= 0 && y + SHIP_H - 1 > rockY[i] && y + 1 < rockY[i] + ROCK_H) return true;
  }
  return false;
}

void updatePlay() {
  if (pressed(B_STAR)) { state = PAUSED; return; }

  // Physics
  thrusting = (nowButtons & (B_UP | B_HASH)) != 0;
  shipV += thrusting ? -5 : 3;
  shipV = constrain(shipV, -36, 40);
  shipY += shipV;

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

  // Difficulty: narrower and faster over time
  uint16_t s = score();
  gap = max(22, 46 - (int)(s / 40));
  speedQ = min(44, 20 + (int)(s / 60));

  if (collides()) {
    state = CRASH;
    crashFrame = 0;
    if (s > best) { best = s; newBest = true; saveBest(); }
  }
}

// ---- Drawing -----------------------------------------------------------------
void drawCave() {
  for (uint8_t c = 0; c < NSEG; c++) {
    uint8_t i = (head + c) % NSEG;
    int16_t x = c * SEG_W - scrollPx;
    if (x >= 128) break;
    u8g2.drawBox(x, 0, SEG_W, caveTop[i]);
    u8g2.drawBox(x, caveBot[i], SEG_W, 64 - caveBot[i]);
    if (rockY[i] >= 0) u8g2.drawBox(x, rockY[i], SEG_W, ROCK_H);
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
  u8g2.setDrawColor(1);
}

void drawCentered(const char* s, uint8_t y) {
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
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
  snprintf(buf, sizeof(buf), "score %u", score());
  drawCentered(buf, 35);
  if (newBest) drawCentered("NEW BEST!", 44);
  else { snprintf(buf, sizeof(buf), "best %u", best); drawCentered(buf, 44); }
  if ((millis() / 500) % 2) drawCentered("# to retry", 52);
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
  if (held(PIN_DOWN)) { best = 0; saveBest(); }  // hold v at power-on to reset
  readButtons();
}

void loop() {
  static uint32_t nextFrame = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextFrame) < 0) return;
  nextFrame = now + 33;  // ~30 fps

  readButtons();
  u8g2.clearBuffer();

  switch (state) {
    case TITLE:
      if (pressed(B_HASH)) newGame();
      drawTitle();
      break;

    case PLAY:
      updatePlay();
      drawCave();
      drawShip(shipY >> 4);
      drawScore();
      break;

    case PAUSED:
      if (pressed(B_STAR)) state = PLAY;
      drawCave();
      drawShip(shipY >> 4);
      drawScore();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      u8g2.setDrawColor(0);
      u8g2.drawBox(30, 22, 68, 20);
      u8g2.setDrawColor(1);
      u8g2.drawFrame(30, 22, 68, 20);
      drawCentered("PAUSED", 37);
      break;

    case CRASH: {  // short explosion animation
      drawCave();
      int8_t y = (shipY >> 4) + 2;
      uint8_t r = 2 + crashFrame * 2;
      u8g2.drawCircle(SHIP_X + 4, y, r);
      if (r > 4) u8g2.drawCircle(SHIP_X + 4, y, r - 4);
      for (uint8_t k = 0; k < 6; k++)
        u8g2.drawPixel(SHIP_X + 4 + random(-r, r + 1), y + random(-r, r + 1));
      drawScore();
      if (++crashFrame > 12) state = GAMEOVER;
      break;
    }

    case GAMEOVER:
      drawCave();
      drawGameOver();
      if (pressed(B_HASH)) newGame();
      break;
  }

  u8g2.sendBuffer();
}
