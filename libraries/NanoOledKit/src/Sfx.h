// Sfx: short sound effects on a passive piezo buzzer, without blocking the game.
//   Wiring: D5 -> 100 ohm -> buzzer + , buzzer - -> GND
// A sound is a PROGMEM list of {frequency Hz, length ms} ending with {0, 0};
// frequency 0 with a length is a rest. Call sfxUpdate() once per frame.
// The mute setting lives in EEPROM address 8 and is shared by all games.
#pragma once
#include <Arduino.h>
#include <EEPROM.h>

const uint8_t PIN_BUZZER = 5;
const int EE_MUTE_ADDR = 8;   // 1 = muted; anything else (new chip: 0xFF) = sound on

struct Note { uint16_t hz, ms; };

namespace sfx_ {
inline const Note*& cur()  { static const Note* v = nullptr; return v; }
inline uint32_t& until()   { static uint32_t v = 0; return v; }
inline bool& muted()       { static bool v = false; return v; }
}  // namespace sfx_

inline void sfxStop() {
  sfx_::cur() = nullptr;
  noTone(PIN_BUZZER);
}

inline void sfxBegin() {
  pinMode(PIN_BUZZER, OUTPUT);
  sfx_::muted() = EEPROM.read(EE_MUTE_ADDR) == 1;
}

inline bool sfxMuted() { return sfx_::muted(); }

inline void sfxSetMuted(bool m) {
  sfx_::muted() = m;
  EEPROM.update(EE_MUTE_ADDR, m ? 1 : 0);
  if (m) sfxStop();
}

inline void sfxNext() {
  using namespace sfx_;
  uint16_t hz = pgm_read_word(&cur()->hz);
  uint16_t ms = pgm_read_word(&cur()->ms);
  if (!ms) { sfxStop(); return; }
  if (hz) tone(PIN_BUZZER, hz, ms); else noTone(PIN_BUZZER);
  until() = millis() + ms;
  cur()++;
}

// Start a sound (replaces the one playing). seq must be in PROGMEM.
inline void sfxPlay(const Note* seq) {
  if (sfx_::muted()) return;
  sfx_::cur() = seq;
  sfxNext();
}

// Start a sound only if nothing else is playing (for repeating ticks).
inline void sfxPlaySoft(const Note* seq) {
  if (!sfx_::cur()) sfxPlay(seq);
}

inline void sfxUpdate() {
  if (sfx_::cur() && (int32_t)(millis() - sfx_::until()) >= 0) sfxNext();
}

// 7x7 speaker icon; a slash through it when muted.
template <class D> void sfxDrawIcon(D& d, uint8_t x, uint8_t y) {
  d.drawBox(x, y + 2, 2, 3);
  d.drawLine(x + 2, y + 2, x + 4, y);
  d.drawLine(x + 2, y + 4, x + 4, y + 6);
  d.drawVLine(x + 4, y, 7);
  if (sfx_::muted()) d.drawLine(x, y + 6, x + 6, y);
  else { d.drawVLine(x + 6, y + 2, 3); }
}
