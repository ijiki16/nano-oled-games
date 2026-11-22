/*
  POCKET KITTY - a virtual pet cat for Arduino Nano + 1.3" SSD1306 OLED

  Take care of Mochi the cat. Mochi has four needs that slowly go down:
    FOOD   - feed her from the bowl
    FUN    - play the catch-the-yarn game
    ENERGY - let her nap (she also dozes off on her own when exhausted)
    CLEAN  - give her a bath (she won't like it)
  When a need gets low, a thought bubble shows what she wants.
  A happy cat gets happy eyes and floating hearts.
  Leave a need at 0 for 2 minutes and she gets sick: FEED her, then let
  her NAP to make her better.
  She grows up: kitten (first 2 hours, with a bow), cat, and after
  24 hours a wise old cat (with glasses).

  Mochi lives in YOUR room: a DHT11 sensor on D2 measures it.
    below 18 C: snow in the window, she shivers, says "brr", gets hungry faster
    27 C and up: sun in the window, she sweats and pants, gets tired faster
  The thermometer sign on the wall and the INFO screen show the room readings.

  Controls (home screen)
    ^ / v    choose menu item (FEED PLAY NAP WASH INFO)
    #        do it
    *        pet Mochi (she purrs)
  Yarn game: ^ = move left, v = move right
  Nap: press any button to wake her up
  INFO: * turns the sound on/off, any other key goes back

  Progress is saved in EEPROM (every 2 minutes and after each action).
  Hold * while powering on to adopt a new kitty (reset).
  After 3 minutes without keys on the home screen the device sleeps
  (screen off, Mochi naps); any key wakes her up.

  Wiring
    OLED SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND
    DHT11 DATA -> D2 (3-pin module), VCC -> 5V, GND -> GND
    K1 (^) -> D12, K2 (v) -> D11, K3 (#) -> D10, K4 (*) -> D9
    Battery gauge (optional): PowerBoost BAT -> 100k -> A1 -> 100k -> GND
      (see "Battery power" in the README; set BATTERY_MONITOR to false if not wired)
    PowerBoost LBO (optional): D3, low-battery warning even without the gauge
    Buzzer (optional): D5 -> 100 ohm -> passive piezo -> GND

  Needs the NanoOledKit library from this repo's libraries/ folder.

  On battery, a battery icon sits in the top-right corner and INFO shows a
  battery bar. Mochi complains when it runs low and saves her progress
  before it runs out.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <EEPROM.h>
#include <NanoOledKit.h>
#include "sprites.h"
#include <DHT.h>

// ---- Room sensor: DHT11 on D2 ----------------------------------------------------
// Mochi feels your real room: below COLD_C she shivers and gets hungry faster,
// at HOT_C and above she pants and gets tired faster.
DHT dht(2, DHT11);
const int8_t NO_READING = -100;
int8_t  roomT = NO_READING;   // degrees C
uint8_t roomH = 0;            // % humidity
const int8_t COLD_C = 18, HOT_C = 27;

bool isCold() { return roomT != NO_READING && roomT < COLD_C; }
bool isHot()  { return roomT >= HOT_C; }

void readRoom() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) { roomT = (int8_t)lround(t); roomH = (uint8_t)lround(h); }
}

// ---- Battery gauge: Li-ion cell through a 100k/100k divider on A1 -------------
// The Nano runs from a 5 V boost converter, so AVcc (the ADC reference) is a
// steady 5 V and A1 sees half the battery voltage.
// An unconnected A1 floats and reads random values: set this to false if the
// divider isn't wired.
const bool BATTERY_MONITOR = true;
const uint8_t PIN_BAT = A1;
const uint16_t VREF_MV = 5000;     // measure the Nano's 5V pin and adjust if the % looks off
int8_t batPct = -1;                // -1 = no battery (USB power)

// Li-ion resting voltage -> charge %, linear between points
const uint16_t BAT_MV[]  = {4200, 4000, 3850, 3750, 3650, 3500, 3300};
const uint8_t  BAT_PCT[] = { 100,   80,   60,   40,   20,    5,    0};

void readBattery() {
  if (!BATTERY_MONITOR) return;
  uint16_t raw = 0;
  for (uint8_t i = 0; i < 8; i++) raw += analogRead(PIN_BAT);
  uint32_t mv = (uint32_t)raw * VREF_MV * 2 / (1023UL * 8);
  if (mv < 2800 || mv > 4500) { batPct = -1; return; }   // not a battery reading
  if (mv >= BAT_MV[0]) { batPct = 100; return; }
  for (uint8_t i = 1; i < sizeof(BAT_MV) / sizeof(BAT_MV[0]); i++) {
    if (mv >= BAT_MV[i]) {
      batPct = BAT_PCT[i] + (int32_t)(mv - BAT_MV[i]) * (BAT_PCT[i - 1] - BAT_PCT[i]) / (BAT_MV[i - 1] - BAT_MV[i]);
      return;
    }
  }
  batPct = 0;
}

// Page mode (_2_): draws the screen in 4 strips, saving 768 bytes of RAM
U8G2_SSD1306_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const uint32_t IDLE_SLEEP_MS = 180000UL;   // 3 minutes on the home screen

// ---- Sounds ------------------------------------------------------------------
const Note SND_MEOW[] PROGMEM   = {{700, 60}, {950, 90}, {760, 140}, {0, 0}};
const Note SND_PURR[] PROGMEM   = {{90, 40}, {0, 30}, {90, 40}, {0, 30}, {90, 40}, {0, 0}};
const Note SND_CLICK[] PROGMEM  = {{2200, 6}, {0, 0}};
const Note SND_CRUNCH[] PROGMEM = {{220, 15}, {0, 0}};
const Note SND_YUM[] PROGMEM    = {{1047, 70}, {1319, 110}, {0, 0}};
const Note SND_CATCH[] PROGMEM  = {{1600, 30}, {2100, 50}, {0, 0}};
const Note SND_DONE[] PROGMEM   = {{1047, 90}, {1319, 90}, {1568, 90}, {2093, 200}, {0, 0}};
const Note SND_SPLASH[] PROGMEM = {{300, 25}, {520, 25}, {340, 25}, {610, 25}, {0, 0}};
const Note SND_HMPH[] PROGMEM   = {{330, 70}, {262, 140}, {0, 0}};
const Note SND_YAWN[] PROGMEM   = {{620, 120}, {520, 120}, {400, 220}, {0, 0}};
const Note SND_SICK[] PROGMEM   = {{494, 150}, {440, 150}, {392, 300}, {0, 0}};
const Note SND_BETTER[] PROGMEM = {{784, 80}, {988, 80}, {1175, 80}, {1568, 200}, {0, 0}};
const Note SND_LOWBAT[] PROGMEM = {{2000, 60}, {0, 60}, {2000, 60}, {0, 0}};
const Note SND_BLIP[] PROGMEM   = {{1500, 40}, {0, 0}};

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
  uint8_t sick;      // 0 = healthy, 1 = sick (needs food), 2 = fed (needs a nap)
};
Save pet;
const int EE_ADDR = 32;
const uint8_t EE_MAGIC = 0x4C;
const uint8_t EE_MAGIC_V1 = 0x4B;   // older save without the sick byte

enum { KITTEN, ADULT, WISE };
uint8_t stage() { return pet.ageMin < 120 ? KITTEN : pet.ageMin < 1440 ? ADULT : WISE; }

void savePet() { EEPROM.put(EE_ADDR, pet); }  // put() only rewrites changed bytes

void adoptNewKitty() {
  pet.magic = EE_MAGIC;
  pet.food = 70; pet.fun = 70; pet.energy = 80; pet.clean = 90;
  pet.ageMin = 0;
  pet.sick = 0;
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
uint8_t zeroSecs = 0;     // seconds with a need at 0 (2 minutes -> sick)
bool lowBat = false;      // battery nearly empty (gauge below 5% or PowerBoost LBO)

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
  if (seconds % 5 == 0) {
    readRoom();
    readBattery();
    // Save once when the battery is nearly empty, before it cuts out
    bool low = (batPct >= 0 && batPct < 5) || lowBattery();
    if (low && !lowBat) { savePet(); sfxPlay(SND_LOWBAT); }
    lowBat = low;
  }
  // A need stuck at 0 for 2 minutes makes her sick
  if (!pet.sick && mode != SLEEPING) {
    bool empty = !pet.food || !pet.fun || !pet.energy || !pet.clean;
    zeroSecs = empty ? zeroSecs + 1 : 0;
    if (zeroSecs >= 120) {
      pet.sick = 1; zeroSecs = 0;
      say("i feel sick..", 70);
      sfxPlay(SND_SICK);
      savePet();
    }
  }
  bool asleep = (mode == SLEEPING);
  uint8_t foodEvery = isCold() ? 4 : 6;      // keeping warm burns calories
  uint8_t energyEvery = isHot() ? 5 : 8;     // heat makes her sluggish
  if (seconds % (asleep ? foodEvery * 2 : foodEvery) == 0) addStat(pet.food, -1);
  uint8_t funEvery = stage() == KITTEN ? 4 : 5;   // kittens want to play more
  if (!asleep && seconds % funEvery == 0) addStat(pet.fun, -1);
  if (!asleep && seconds % energyEvery == 0) addStat(pet.energy, -1);
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
  // window: snow falls outside when the room is cold, the sun shines when hot
  u8g2.drawFrame(72, 13, 22, 18);
  if (isCold()) {
    for (uint8_t i = 0; i < 5; i++)
      u8g2.drawPixel(74 + i * 4 + (i % 2), 14 + (frame / 6 + i * 5) % 16);
  } else if (isHot()) {
    u8g2.drawDisc(83, 22, 4);
  }
  u8g2.drawVLine(82, 13, 18);
  u8g2.drawHLine(72, 21, 22);
  // room thermometer sign
  char buf[8];
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawRFrame(101, 12, 26, 20, 2);
  if (roomT == NO_READING) strcpy(buf, "--C");
  else snprintf(buf, sizeof(buf), "%dC", roomT);
  u8g2.drawStr(104, 20, buf);
  if (roomT == NO_READING) strcpy(buf, "--%");
  else snprintf(buf, sizeof(buf), "%u%%", roomH);
  u8g2.drawStr(104, 29, buf);
  // little rug
  for (uint8_t x = 4; x < 30; x += 3) u8g2.drawPixel(x, FLOOR_Y + 2 + (x / 3) % 2);
}

void drawCat(int16_t x, const uint8_t* bits, int8_t bob) {
  int16_t y = CAT_Y + bob;
  u8g2.drawXBMP(x, y, CAT_OPEN_W, CAT_OPEN_H, bits);
  if (stage() == KITTEN) {          // a bow between the ears
    u8g2.drawBox(x + 8, y, 3, 4);
    u8g2.drawBox(x + 13, y, 3, 4);
    u8g2.drawBox(x + 11, y + 1, 2, 2);
  } else if (stage() == WISE) {     // round glasses, drawn in black on her white face
    u8g2.setDrawColor(0);
    u8g2.drawFrame(x + 4, y + 5, 6, 6);
    u8g2.drawFrame(x + 14, y + 5, 6, 6);
    u8g2.drawHLine(x + 10, y + 6, 4);
    u8g2.setDrawColor(1);
  }
}

// Small battery icon (10x5), filled by charge; blinks when almost empty
void drawBattery(uint8_t x, uint8_t y) {
  if (batPct < 0) {  // no gauge: the PowerBoost's LBO line can still warn
    if (lowBat && (frame / 15) % 2) drawLowBattery(u8g2, x, y);
    return;
  }
  if (batPct < 10 && (frame / 15) % 2) return;
  u8g2.drawFrame(x, y, 9, 5);
  u8g2.drawVLine(x + 9, y + 1, 3);
  u8g2.drawBox(x + 1, y + 1, (batPct * 7 + 50) / 100, 3);
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
    x += w + 4;
  }
  drawBattery(117, 2);
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
    case 9:  // sick: a thermometer
      u8g2.drawFrame(bx + 7, 18, 3, 6);
      u8g2.drawDisc(bx + 8, 24, 2);
      break;
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
  u8g2.drawFrame(44, y, 72, 7);
  u8g2.drawBox(46, y + 2, (v * 68) / 100, 3);
}

// ---- Modes ---------------------------------------------------------------------
void updateHome() {
  if (repeat(B_UP))   { menuSel = (menuSel + MENU_N - 1) % MENU_N; sfxPlay(SND_CLICK); }
  if (repeat(B_DOWN)) { menuSel = (menuSel + 1) % MENU_N; sfxPlay(SND_CLICK); }

  if (pressed(B_STAR) && !petCooldown) {
    addStat(pet.fun, 3);
    spawnParticle(catX + 8 + random(8), CAT_Y - 2, 0);
    say(random(2) ? "prrr" : "purr~", 40);
    sfxPlay(SND_PURR);
    petCooldown = 20;
  }
  if (petCooldown) petCooldown--;

  if (pressed(B_HASH)) {
    switch (menuSel) {
      case 0:  // FEED
        if (pet.food > 85 && pet.sick != 1) say("full!", 50);
        else setMode(EATING);
        break;
      case 1:  // PLAY
        if (pet.sick) say("too sick..", 60);
        else if (pet.energy < 15) say("too sleepy", 60);
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

  // Room temperature comments
  if (!speechT && random(400) == 0) {
    if (isCold()) say(random(2) ? "brr..." : "so cold!", 45);
    else if (isHot()) say(random(2) ? "so hot~" : "*pant*", 45);
  }

  // Battery running low
  if (!speechT) {
    if (lowBat && random(150) == 0) say("charge me!", 50);
    else if (batPct >= 0 && batPct < 15 && random(400) == 0) say("low battery!", 50);
  }

  // Sick kitties feel sorry for themselves
  if (pet.sick) {
    if (!speechT && random(300) == 0) say(pet.sick == 1 ? "feed me.." : "need a nap..", 50);
    return;
  }

  // Mood moments
  if (random(250) == 0) {
    uint8_t m = mood();
    if (m >= 70) spawnParticle(catX + 8 + random(8), CAT_Y - 2, 0);
    else if (m < 30 && !speechT) say("meow...", 50);
    else if (!speechT && random(3) == 0) { say("meow!", 30); sfxPlay(SND_MEOW); }
  }
}

void drawHome() {
  drawRoom();
  bool walking = catX != targetX;
  int8_t bob = walking ? (frame / 4) % 2 : 0;
  const uint8_t* face = cat_open_bits;
  if (blinkT || pet.sick) face = cat_blink_bits;
  else if (mood() >= 70 && (frame / 90) % 3 == 0) face = cat_happy_bits;
  else if (speechT && (speech[0] == 'p')) face = cat_happy_bits;  // purring
  // Shiver in short bursts when cold
  int8_t shiver = (isCold() && !walking && (frame / 30) % 4 == 0) ? (frame % 2) : 0;
  drawCat(catX + shiver, face, bob);
  if (isHot()) {
    // sweat drop sliding down her cheek
    uint8_t dy = (frame / 6) % 6;
    u8g2.drawPixel(catX + 23, CAT_Y + 3 + dy);
    u8g2.drawPixel(catX + 23, CAT_Y + 4 + dy);
    u8g2.drawPixel(catX + 22, CAT_Y + 4 + dy);
  }
  drawParticles();
  if (speechT) drawSpeech(catX + 12);
  else {
    uint8_t need = pet.sick ? 9 : lowestNeed();
    if (need != 255 && (frame / 60) % 2) drawThought(need);
  }
  drawMenu();
}

void updateEating() {
  if (modeT < 90 && modeT % 12 == 0) sfxPlay(SND_CRUNCH);
  if (modeT == 100) {
    addStat(pet.food, 35);
    if (pet.sick == 1) { pet.sick = 2; say("thanks.. sleepy", 60); }
    else say("yum!", 45);
    sfxPlay(SND_YUM);
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
  if (held(B_UP))   catX = max(0, catX - 3);
  if (held(B_DOWN)) catX = min(128 - CAT_OPEN_W, catX + 3);

  yarnY += yarnV;
  int16_t yx = yarnX >> 4, yy = yarnY >> 4;
  bool landed = false;
  if (yy + YARN_H >= CAT_Y + 2 && yx + YARN_W > catX + 2 && yx < catX + CAT_OPEN_W - 2) {
    caught++;
    spawnParticle(catX + 8, CAT_Y - 4, 0);
    sfxPlay(SND_CATCH);
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
    sfxPlay(SND_DONE);
    setMode(PLAY_DONE);
  }
}

void drawPlaying() {
  u8g2.drawHLine(0, FLOOR_Y + 1, 128);
  int16_t yx = yarnX >> 4, yy = yarnY >> 4;
  u8g2.drawLine(yx + 4, yy, yx + 4 + ((frame / 5) % 3) - 1, 10);  // the string
  u8g2.drawXBMP(yx, yy, YARN_W, YARN_H, yarn_bits);
  drawCat(catX, held(B_UP | B_DOWN) ? cat_happy_bits : cat_open_bits, (frame / 4) % 2);
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
    if (pet.sick == 2 && pet.energy >= 60) {
      pet.sick = 0;
      say("all better!", 60);
      sfxPlay(SND_BETTER);
    } else {
      say(pet.energy >= 100 ? "*yawn* hi!" : "*yawn*", 50);
      sfxPlay(SND_YAWN);
    }
    savePet();
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
  if (modeT % 30 == 1) sfxPlay(SND_SPLASH);
  if (modeT == 120) {
    pet.clean = 100;
    addStat(pet.fun, -5);
    say("hmph!", 50);
    sfxPlay(SND_HMPH);
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
  // beside the tub, below the window and the thermometer sign
  u8g2.drawStr(catX < 70 ? catX + 32 : catX - 30, 50, "scrub");
}

void drawInfo() {
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[26];
  const char* who = stage() == KITTEN ? "KITTEN" : stage() == WISE ? "WISE CAT" : "MOCHI";
  uint16_t age = pet.ageMin < 120 ? pet.ageMin : pet.ageMin / 60;
  char unit = pet.ageMin < 120 ? 'm' : 'h';
  if (roomT == NO_READING) snprintf(buf, sizeof(buf), "%s %u%c  room --", who, age, unit);
  else snprintf(buf, sizeof(buf), "%s %u%c  %dC %u%%", who, age, unit, roomT, roomH);
  u8g2.drawStr(2, 7, buf);
  sfxDrawIcon(u8g2, 120, 11);   // * turns the sound on/off
  u8g2.drawHLine(0, 9, 128);
  drawBar(11, "food", pet.food);
  drawBar(20, "fun", pet.fun);
  drawBar(29, "energy", pet.energy);
  drawBar(38, "clean", pet.clean);
  if (batPct >= 0) drawBar(47, "battery", batPct);
  uint8_t m = mood();
  snprintf(buf, sizeof(buf), "mood: %s%s",
           pet.sick ? "sick" : m >= 70 ? "very happy" : m >= 40 ? "ok" : "sad",
           isCold() ? " (cold)" : isHot() ? " (hot)" : "");
  drawCentered(buf, 63);
}

// ---- Main --------------------------------------------------------------------
void setup() {
  keysBegin();
  sfxBegin();
  powerBegin();

  u8g2.setBusClock(400000);
  u8g2.begin();
  randomSeed(analogRead(A0) ^ micros());
  dht.begin();
  readBattery();

  EEPROM.get(EE_ADDR, pet);
  if (pet.magic == EE_MAGIC_V1) {   // save from before the sick byte: keep the pet
    pet.magic = EE_MAGIC;
    pet.sick = 0;
    savePet();
  }
  bool fresh = pet.magic != EE_MAGIC || held(B_STAR);
  if (fresh) adoptNewKitty();
  say(fresh ? "hi! i'm mochi" : "meow! hi!", 70);
  sfxPlay(SND_MEOW);
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
      u8g2.setFont(u8g2_font_5x7_tr);
      {  // banner with its own background so it doesn't mix with the window
        uint8_t tw = u8g2.getStrWidth(resultText);
        uint8_t w = tw + 12;
        uint8_t x = (128 - w) / 2;
        u8g2.setDrawColor(0); u8g2.drawBox(x, 8, w, 17); u8g2.setDrawColor(1);
        u8g2.drawRFrame(x, 8, w, 17, 4);
        u8g2.drawStr(x + 6, 20, resultText);   // drawn twice, 1 px apart: bold
        u8g2.drawStr(x + 7, 20, resultText);
      }
      break;
  }
}

void loop() {
  static uint32_t nextFrame = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextFrame) < 0) return;
  nextFrame = now + 33;  // ~30 fps

  readKeys();
  sfxUpdate();
  tickNeeds();
  updateParticles();
  if (speechT) speechT--;
  modeT++;

  // Game logic first...
  switch (mode) {
    case HOME:
      updateHome();
      // Long idle: save, screen off, chip asleep. Mochi naps until a key wakes her.
      if (mode == HOME && idleMs() >= IDLE_SLEEP_MS) {
        savePet();
        powerSleep(u8g2);
        say("*yawn* hi!", 60);
        sfxPlay(SND_YAWN);
      }
      break;
    case EATING:   updateEating();   break;
    case PLAYING:  updatePlaying();  break;
    case SLEEPING: updateSleeping(); break;
    case BATH:     updateBath();     break;
    case INFO:
      if (modeT > 5 && pressed(B_STAR)) { sfxSetMuted(!sfxMuted()); sfxPlay(SND_BLIP); }
      else if (modeT > 5 && pressed(B_ANY)) setMode(HOME);
      break;
    case PLAY_DONE: if (modeT > 75) setMode(HOME); break;
  }

  // ...then draw. In page mode the draw code runs once per strip,
  // so drawing functions must not change any game state.
  u8g2.firstPage();
  do { drawScreen(); } while (u8g2.nextPage());
}
