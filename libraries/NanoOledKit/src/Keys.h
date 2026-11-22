// Keys: the module's 4 push buttons, read once per frame.
//   K1 (^) D12, K2 (v) D11, K3 (#) D10, K4 (*) D9 - active LOW with INPUT_PULLUP.
// All four sit on AVR port B (PB4..PB1), so one pin-change interrupt can wake the
// chip from sleep (see Power.h).
#pragma once
#include <Arduino.h>

enum : uint8_t { B_UP = 1, B_DOWN = 2, B_HASH = 4, B_STAR = 8, B_ANY = 15 };
const uint8_t PIN_UP = 12, PIN_DOWN = 11, PIN_HASH = 10, PIN_STAR = 9;

const uint16_t KEY_REPEAT_DELAY_MS = 400;  // hold this long before auto-repeat starts
const uint16_t KEY_REPEAT_EVERY_MS = 100;

namespace keys_ {
inline uint8_t& now()      { static uint8_t v = 0; return v; }
inline uint8_t& prev()     { static uint8_t v = 0; return v; }
inline uint8_t& ignore()   { static uint8_t v = 0; return v; }  // held at wake: ignored until released
inline uint32_t& active()  { static uint32_t v = 0; return v; } // last time any key was down
inline uint32_t& downAt()  { static uint32_t v = 0; return v; } // when the current keys went down
inline uint32_t& nextRep() { static uint32_t v = 0; return v; }
inline uint8_t raw() {
  return (digitalRead(PIN_UP)   == LOW ? B_UP   : 0) |
         (digitalRead(PIN_DOWN) == LOW ? B_DOWN : 0) |
         (digitalRead(PIN_HASH) == LOW ? B_HASH : 0) |
         (digitalRead(PIN_STAR) == LOW ? B_STAR : 0);
}
}  // namespace keys_

inline void keysBegin() {
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_HASH, INPUT_PULLUP);
  pinMode(PIN_STAR, INPUT_PULLUP);
  keys_::now() = keys_::prev() = keys_::raw();
  keys_::active() = millis();
}

// Call once per frame (the games run at ~30 fps, which also debounces the keys).
inline void readKeys() {
  using namespace keys_;
  prev() = now();
  uint8_t r = raw();
  ignore() &= r;            // a swallowed key counts again once it is released
  now() = r & ~ignore();
  uint32_t t = millis();
  if (r) active() = t;
  if (now() & ~prev()) { downAt() = t; nextRep() = t + KEY_REPEAT_DELAY_MS; }
}

inline bool pressed(uint8_t b) { return keys_::now() & ~keys_::prev() & b; }  // went down this frame
inline bool held(uint8_t b)    { return keys_::now() & b; }                    // is down now

// pressed(), plus auto-repeat while held: for menus and lists.
inline bool repeat(uint8_t b) {
  using namespace keys_;
  if (pressed(b)) return true;
  if (!(now() & b)) return false;
  uint32_t t = millis();
  if ((int32_t)(t - nextRep()) < 0) return false;
  nextRep() = t + KEY_REPEAT_EVERY_MS;
  return true;
}

// Milliseconds since a key was last down (for sleep after idle).
inline uint32_t idleMs() { return millis() - keys_::active(); }

// Ignore the keys that are down right now until they are released (after waking up).
inline void keysSwallow() {
  keys_::ignore() = keys_::raw();
  keys_::now() = keys_::prev() = 0;
  keys_::active() = millis();
}
