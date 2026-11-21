/*
  POCKET KITTY - a virtual pet cat for Arduino Nano + 1.3" SSD1306 OLED

  Take care of Mochi the cat. Mochi has four needs that slowly go down:
    FOOD   - feed her from the bowl
    FUN    - play the catch-the-yarn game
    ENERGY - let her nap (she also dozes off on her own when exhausted)
    CLEAN  - give her a bath (she won't like it)
  When a need gets low, a thought bubble shows what she wants.
  A happy cat gets happy eyes and floating hearts.

  Controls (home screen)
    ^ / v    choose menu item (FEED PLAY NAP WASH INFO)
    #        do it
    *        pet Mochi (she purrs)
  Yarn game: ^ = move left, v = move right
  Nap: press any button to wake her up

  Progress is saved in EEPROM (every 2 minutes and after each action).
  Hold * while powering on to adopt a new kitty (reset).

  Wiring
    OLED SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
    K1 (^) -> D12, K2 (v) -> D11, K3 (#) -> D10, K4 (*) -> D9
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include "sprites.h"

// Page mode (_2_): draws the screen in 4 strips, saving 768 bytes of RAM
U8G2_SSD1306_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---- Buttons ---------------------------------------------------------------
const uint8_t PIN_UP = 12, PIN_DOWN = 11, PIN_HASH = 10, PIN_STAR = 9;
enum { B_UP = 1, B_DOWN = 2, B_HASH = 4, B_STAR = 8, B_ANY = 15 };
uint8_t nowButtons = 0, prevButtons = 0;

void readButtons() {
  prevButtons = nowButtons;
  nowButtons = (digitalRead(PIN_UP)   == LOW ? B_UP   : 0) |
               (digitalRead(PIN_DOWN) == LOW ? B_DOWN : 0) |
               (digitalRead(PIN_HASH) == LOW ? B_HASH : 0) |
               (digitalRead(PIN_STAR) == LOW ? B_STAR : 0);
}
bool pressed(uint8_t b) { return (nowButtons & b) & ~prevButtons; }
bool isHeld(uint8_t b)  { return nowButtons & b; }

// ---- Layout ------------------------------------------------------------------
const uint8_t FLOOR_Y = 61;
const uint8_t CAT_Y = FLOOR_Y - CAT_OPEN_H;   // top of the sitting cat
const char* const MENU[] = {"FEED", "PLAY", "NAP", "WASH", "INFO"};
const uint8_t MENU_N = 5;

// ---- Pet state (saved) -------------------------------------------------------
struct Save {
  uint8_t magic;
  uint8_t food, fun, energy, clean;
  uint16_t ageMin;
};
Save pet;
const int EE_ADDR = 32;
const uint8_t EE_MAGIC = 0x4B;

void savePet() { EEPROM.put(EE_ADDR, pet); }  // put() only rewrites changed bytes

void adoptNewKitty() {
  pet.magic = EE_MAGIC;
  pet.food = 70; pet.fun = 70; pet.energy = 80; pet.clean = 90;
  pet.ageMin = 0;
  savePet();
}

void addStat(uint8_t& s, int d) { s = constrain((int)s + d, 0, 100); }

// ---- Runtime state -----------------------------------------------------------
enum Mode { HOME, EATING, PLAYING, SLEEPING, BATH, INFO, PLAY_DONE };
Mode mode = HOME;
uint8_t menuSel = 0;
uint16_t frame;
uint16_t modeT;           // frames since entering the current mode
uint16_t seconds;         // play-time seconds (for decay + age)

int16_t catX = 52, targetX = 52;
uint8_t walkPause = 60;
uint8_t blinkT = 0;

const char* speech = nullptr;
uint8_t speechT = 0;

uint8_t petCooldown = 0;

// floating hearts and Zs
struct Particle { int8_t x, y; uint8_t t, kind; };  // kind 0 = heart, 1 = z
const uint8_t MAX_P = 6;
Particle parts[MAX_P];

// yarn mini-game
int16_t yarnX, yarnY, yarnV;  // yarn position/speed in 1/16 px
uint8_t caught, missed;
char resultText[16];

// ---- Helpers -------------------------------------------------------------------
void say(const char* s, uint8_t frames) { speech = s; speechT = frames; }

void spawnParticle(int8_t x, int8_t y, uint8_t kind) {
  for (uint8_t i = 0; i < MAX_P; i++)
    if (!parts[i].t) { parts[i].x = x; parts[i].y = y; parts[i].t = 40; parts[i].kind = kind; return; }
}

void setMode(uint8_t m) { mode = (Mode)m; modeT = 0; }

uint8_t mood() { return ((uint16_t)pet.food + pet.fun + pet.energy + pet.clean) / 4; }

// Which need is lowest? returns 0..3, or 255 if none is low
uint8_t lowestNeed() {
  uint8_t v[4] = {pet.food, pet.fun, pet.energy, pet.clean};
  uint8_t best = 255, bestV = 35;
  for (uint8_t i = 0; i < 4; i++) if (v[i] < bestV) { bestV = v[i]; best = i; }
  return best;
}

// Needs go down once per second-tick
void tickNeeds() {
  if (++frame % 30) return;
  seconds++;
  bool asleep = (mode == SLEEPING);
  if (seconds % (asleep ? 12 : 6) == 0) addStat(pet.food, -1);
  if (!asleep && seconds % 5 == 0) addStat(pet.fun, -1);
  if (!asleep && seconds % 8 == 0) addStat(pet.energy, -1);
  if (asleep && seconds % 2 == 0) addStat(pet.energy, +1);
  if (seconds % 10 == 0) addStat(pet.clean, -1);
  if (seconds % 60 == 0) pet.ageMin++;
  if (seconds % 120 == 0) savePet();
}

// ---- Drawing -------------------------------------------------------------------
void drawCentered(const char* s, uint8_t y) {
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
}

void drawRoom() {
  u8g2.drawHLine(0, FLOOR_Y + 1, 128);
  // window
  u8g2.drawFrame(100, 13, 22, 18);
  u8g2.drawVLine(110, 13, 18);
  u8g2.drawHLine(100, 21, 22);
  // little rug
  for (uint8_t x = 4; x < 30; x += 3) u8g2.drawPixel(x, FLOOR_Y + 2 + (x / 3) % 2);
}

void drawCat(int16_t x, const uint8_t* bits, int8_t bob) {
  u8g2.drawXBMP(x, CAT_Y + bob, CAT_OPEN_W, CAT_OPEN_H, bits);
}

void drawMenu() {
  u8g2.setFont(u8g2_font_5x7_tr);
  uint8_t x = 1;
  for (uint8_t i = 0; i < MENU_N; i++) {
    uint8_t w = u8g2.getStrWidth(MENU[i]);
    if (i == menuSel) {
      u8g2.drawBox(x - 1, 0, w + 3, 9);
      u8g2.setDrawColor(0);
      u8g2.drawStr(x + 1, 7, MENU[i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(x + 1, 7, MENU[i]);
    }
    x += w + 6;
  }
}

void drawSpeech(int16_t cx) {
  u8g2.setFont(u8g2_font_5x7_tr);
  uint8_t w = u8g2.getStrWidth(speech) + 6;
  int16_t x = constrain(cx - w / 2, 0, 127 - w);
  u8g2.setDrawColor(0); u8g2.drawBox(x, 23, w, 11); u8g2.setDrawColor(1);
  u8g2.drawRFrame(x, 23, w, 11, 3);
  u8g2.drawStr(x + 3, 31, speech);
}

void drawThought(uint8_t need) {
  // bubble on whichever side has room
  bool right = catX < 70;
  int16_t bx = right ? catX + 26 : catX - 20;
  int16_t dx = right ? catX + 23 : catX + 1;
  u8g2.drawDisc(dx, CAT_Y - 1, 1);
  u8g2.drawCircle(dx + (right ? 3 : -3), CAT_Y - 5, 2);
  u8g2.setDrawColor(0); u8g2.drawBox(bx, 16, 16, 12); u8g2.setDrawColor(1);
  u8g2.drawRFrame(bx, 16, 16, 12, 3);
  u8g2.setFont(u8g2_font_5x7_tr);
  switch (need) {
    case 0: u8g2.drawXBMP(bx + 3, 20, FISH_W, FISH_H, fish_bits); break;
    case 1: u8g2.drawXBMP(bx + 4, 18, YARN_W, YARN_H, yarn_bits); break;
    case 2: u8g2.drawStr(bx + 4, 25, "zz"); break;
    case 3: u8g2.drawStr(bx + 3, 25, "!?"); break;
  }
}

void drawParticles() {
  u8g2.setFont(u8g2_font_5x7_tr);
  for (uint8_t i = 0; i < MAX_P; i++) {
    Particle& p = parts[i];
    if (!p.t) continue;
    if (p.kind == 0) u8g2.drawXBMP(p.x, p.y, HEART_W, HEART_H, heart_bits);
    else u8g2.drawStr(p.x, p.y, p.t > 20 ? "z" : "Z");
  }
}

void updateParticles() {
  for (uint8_t i = 0; i < MAX_P; i++) {
    Particle& p = parts[i];
    if (!p.t) continue;
    p.t--;
    if (p.t % 3 == 0) p.y--;
    if (p.kind == 1 && p.t % 8 == 0) p.x += (p.t / 8) % 2 ? 1 : -1;  // Zs sway
  }
}

void drawBar(uint8_t y, const char* label, uint8_t v) {
  u8g2.drawStr(2, y + 6, label);
  u8g2.drawFrame(44, y, 80, 7);
  u8g2.drawBox(46, y + 2, (v * 76) / 100, 3);
}

// ---- Modes ---------------------------------------------------------------------
void updateHome() {
  if (pressed(B_UP))   menuSel = (menuSel + MENU_N - 1) % MENU_N;
  if (pressed(B_DOWN)) menuSel = (menuSel + 1) % MENU_N;

  if (pressed(B_STAR) && !petCooldown) {
    addStat(pet.fun, 3);
    spawnParticle(catX + 8 + random(8), CAT_Y - 2, 0);
    say(random(2) ? "prrr" : "purr~", 40);
    petCooldown = 20;
  }
  if (petCooldown) petCooldown--;

  if (pressed(B_HASH)) {
    switch (menuSel) {
      case 0:  // FEED
        if (pet.food > 85) say("full!", 50);
        else setMode(EATING);
        break;
      case 1:  // PLAY
        if (pet.energy < 15) say("too sleepy", 60);
        else {
          setMode(PLAYING);
          caught = missed = 0;
          yarnX = random(4, 116) * 16; yarnY = 10 * 16; yarnV = 16;
        }
        break;
      case 2: setMode(SLEEPING); break;
      case 3: setMode(BATH); break;
      case 4: setMode(INFO); break;
    }
    return;
  }

  // Exhausted kitties fall asleep on their own
  if (pet.energy < 5) { say("so tired..", 45); setMode(SLEEPING); return; }

  // Wander around the room
  if (catX != targetX) {
    if (frame % 2 == 0) catX += (targetX > catX) ? 1 : -1;
  } else if (walkPause) {
    walkPause--;
  } else {
    targetX = random(0, 100);
    walkPause = 60 + random(120);
  }

  if (blinkT) blinkT--;
  else if (random(100) == 0) blinkT = 5;

  // Mood moments
  if (random(250) == 0) {
    uint8_t m = mood();
    if (m >= 70) spawnParticle(catX + 8 + random(8), CAT_Y - 2, 0);
    else if (m < 30 && !speechT) say("meow...", 50);
    else if (!speechT && random(3) == 0) say("meow!", 30);
  }
}

void drawHome() {
  drawRoom();
  bool walking = catX != targetX;
  int8_t bob = walking ? (frame / 4) % 2 : 0;
  const uint8_t* face = cat_open_bits;
  if (blinkT) face = cat_blink_bits;
  else if (mood() >= 70 && (frame / 90) % 3 == 0) face = cat_happy_bits;
  else if (speechT && (speech[0] == 'p')) face = cat_happy_bits;  // purring
  drawCat(catX, face, bob);
  drawParticles();
  if (speechT) drawSpeech(catX + 12);
  else {
    uint8_t need = lowestNeed();
    if (need != 255 && (frame / 60) % 2) drawThought(need);
  }
  drawMenu();
}

void updateEating() {
  if (modeT == 100) {
    addStat(pet.food, 35);
    say("yum!", 45);
    spawnParticle(catX + 8, CAT_Y - 2, 0);
    savePet();
    setMode(HOME);
  }
}

void drawEating() {
  drawRoom();
  bool bowlRight = catX < 90;
  int16_t bx = bowlRight ? catX + 25 : catX - 13;
  u8g2.drawXBMP(bx, FLOOR_Y - BOWL_H + 1, BOWL_W, BOWL_H, bowl_bits);
  // kibble disappears as she eats
  if (modeT > 50) { u8g2.setDrawColor(0); u8g2.drawHLine(bx, FLOOR_Y - BOWL_H + 1, BOWL_W); u8g2.setDrawColor(1); }
  int8_t bob = ((modeT / 6) % 2) * 2;  // nom nom
  drawCat(catX, cat_blink_bits, bob);
  u8g2.setFont(u8g2_font_5x7_tr);
  if ((modeT / 10) % 2) u8g2.drawStr(bowlRight ? bx + 2 : bx, FLOOR_Y - 9, "nom");
}

void updatePlaying() {
  // ^ = left, v = right
  if (isHeld(B_UP))   catX = max(0, catX - 3);
  if (isHeld(B_DOWN)) catX = min(128 - CAT_OPEN_W, catX + 3);

  yarnY += yarnV;
  int16_t yx = yarnX >> 4, yy = yarnY >> 4;
  bool landed = false;
  if (yy + YARN_H >= CAT_Y + 2 && yx + YARN_W > catX + 2 && yx < catX + CAT_OPEN_W - 2) {
    caught++;
    spawnParticle(catX + 8, CAT_Y - 4, 0);
    landed = true;
  } else if (yy + YARN_H >= FLOOR_Y) {
    missed++;
    landed = true;
  }
  if (landed) {
    yarnX = random(0, 120) * 16;
    yarnY = 10 * 16;
    yarnV = min(48, 16 + caught * 3);
  }

  if (modeT >= 15 * 30) {  // 15 seconds
    addStat(pet.fun, caught * 6);
    addStat(pet.energy, -(int)caught);
    addStat(pet.food, -3);
    snprintf(resultText, sizeof(resultText), "caught %u!", caught);
    savePet();
    setMode(PLAY_DONE);
  }
}

void drawPlaying() {
  u8g2.drawHLine(0, FLOOR_Y + 1, 128);
  int16_t yx = yarnX >> 4, yy = yarnY >> 4;
  u8g2.drawLine(yx + 4, yy, yx + 4 + ((frame / 5) % 3) - 1, 10);  // the string
  u8g2.drawXBMP(yx, yy, YARN_W, YARN_H, yarn_bits);
  drawCat(catX, isHeld(B_UP | B_DOWN) ? cat_happy_bits : cat_open_bits, (frame / 4) % 2);
  drawParticles();
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[12];
  snprintf(buf, sizeof(buf), "yarn %u", caught);
  u8g2.drawStr(1, 7, buf);
  snprintf(buf, sizeof(buf), "%us", 15 - modeT / 30);
  u8g2.drawStr(110, 7, buf);
  if (modeT < 60) drawCentered("^ left  v right", 30);
}

void updateSleeping() {
  if (modeT % 45 == 0) spawnParticle(catX + 18, CAT_Y + 6, 1);
  if (modeT > 15 && (pressed(B_ANY) || pet.energy >= 100)) {
    savePet();
    say(pet.energy >= 100 ? "*yawn* hi!" : "*yawn*", 50);
    setMode(HOME);
  }
}

void drawSleeping() {
  // night: moon and stars
  u8g2.drawDisc(16, 14, 7);
  u8g2.setDrawColor(0); u8g2.drawDisc(19, 12, 6); u8g2.setDrawColor(1);
  const uint8_t stars[][2] = {{40, 6}, {70, 14}, {95, 5}, {118, 18}, {55, 24}};
  for (uint8_t i = 0; i < 5; i++)
    if ((frame / 20 + i) % 4) u8g2.drawPixel(stars[i][0], stars[i][1]);
  u8g2.drawHLine(0, FLOOR_Y + 1, 128);
  // gentle breathing
  int8_t breathe = (modeT / 30) % 2;
  u8g2.drawXBMP(catX, FLOOR_Y - CAT_SLEEP_H + 1 - breathe, CAT_SLEEP_W, CAT_SLEEP_H, cat_sleep_bits);
  drawParticles();
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[16];
  snprintf(buf, sizeof(buf), "energy %u", pet.energy);
  u8g2.drawStr(127 - u8g2.getStrWidth(buf), 7, buf);
}

void updateBath() {
  if (modeT == 120) {
    pet.clean = 100;
    addStat(pet.fun, -5);
    say("hmph!", 50);
    savePet();
    setMode(HOME);
  }
}

void drawBath() {
  drawRoom();
  // tub
  drawCat(catX, cat_blink_bits, 0);
  u8g2.setDrawColor(0); u8g2.drawBox(catX - 4, FLOOR_Y - 9, 32, 10); u8g2.setDrawColor(1);
  u8g2.drawRFrame(catX - 4, FLOOR_Y - 10, 32, 11, 3);
  // bubbles drift up
  randomSeed(modeT / 4);
  for (uint8_t i = 0; i < 7; i++) {
    int16_t bx = catX - 4 + random(32);
    int16_t by = FLOOR_Y - 12 - random(30);
    u8g2.drawCircle(bx, by, 1 + random(3));
  }
  randomSeed(micros());
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(catX < 70 ? catX + 32 : catX - 32, 28, "scrub");
}

void drawInfo() {
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[22];
  snprintf(buf, sizeof(buf), "MOCHI  age %u min", pet.ageMin);
  u8g2.drawStr(2, 7, buf);
  u8g2.drawHLine(0, 9, 128);
  drawBar(13, "food", pet.food);
  drawBar(23, "fun", pet.fun);
  drawBar(33, "energy", pet.energy);
  drawBar(43, "clean", pet.clean);
  uint8_t m = mood();
  drawCentered(m >= 70 ? "mood: very happy" : m >= 40 ? "mood: ok" : "mood: sad", 62);
}

// ---- Main --------------------------------------------------------------------
void setup() {
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_HASH, INPUT_PULLUP);
  pinMode(PIN_STAR, INPUT_PULLUP);

  u8g2.setBusClock(400000);
  u8g2.begin();
  randomSeed(analogRead(A0) ^ micros());

  EEPROM.get(EE_ADDR, pet);
  bool fresh = pet.magic != EE_MAGIC || digitalRead(PIN_STAR) == LOW;
  if (fresh) adoptNewKitty();
  say(fresh ? "hi! i'm mochi" : "meow! hi!", 70);
  readButtons();
}

void drawScreen() {
  switch (mode) {
    case HOME:     drawHome();     break;
    case EATING:   drawEating();   break;
    case PLAYING:  drawPlaying();  break;
    case SLEEPING: drawSleeping(); break;
    case BATH:     drawBath();     break;
    case INFO:     drawInfo();     break;
    case PLAY_DONE:
      drawRoom();
      drawCat(catX, cat_happy_bits, 0);
      drawParticles();
      u8g2.setFont(u8g2_font_ncenB10_tr);
      drawCentered(resultText, 22);
      break;
  }
}

void loop() {
  static uint32_t nextFrame = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextFrame) < 0) return;
  nextFrame = now + 33;  // ~30 fps

  readButtons();
  tickNeeds();
  updateParticles();
  if (speechT) speechT--;
  modeT++;

  // Game logic first...
  switch (mode) {
    case HOME:     updateHome();     break;
    case EATING:   updateEating();   break;
    case PLAYING:  updatePlaying();  break;
    case SLEEPING: updateSleeping(); break;
    case BATH:     updateBath();     break;
    case INFO:      if (modeT > 5 && pressed(B_ANY)) setMode(HOME); break;
    case PLAY_DONE: if (modeT > 75) setMode(HOME); break;
  }

  // ...then draw. In page mode the draw code runs once per strip,
  // so drawing functions must not change any game state.
  u8g2.firstPage();
  do { drawScreen(); } while (u8g2.nextPage());
}
