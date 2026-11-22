// Minimal fake Arduino core so the sketches can run on a PC and render
// into U8g2's memory buffer. Time, buttons and sensors are scripted.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <type_traits>

typedef bool boolean;
typedef uint8_t byte;
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define A0 14
#define A4 18
#define A5 19
#define PROGMEM
#define F(s) (s)
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

template <class A, class B> inline typename std::common_type<A, B>::type min(A a, B b) { return a < b ? a : b; }
template <class A, class B> inline typename std::common_type<A, B>::type max(A a, B b) { return a > b ? a : b; }

extern uint32_t g_ms;
extern uint8_t g_buttons;  // bit0 ^ D12, bit1 v D11, bit2 # D10, bit3 * D9

inline uint32_t millis() { return g_ms; }
inline uint32_t micros() { return g_ms * 1000UL + 123; }
inline void delay(uint32_t ms) { g_ms += ms; }
inline void pinMode(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t pin) {
  uint8_t bit = pin == 12 ? 1 : pin == 11 ? 2 : pin == 10 ? 4 : pin == 9 ? 8 : 0;
  return (g_buttons & bit) ? LOW : HIGH;
}
inline int analogRead(uint8_t) { return 512; }
inline void randomSeed(unsigned long s) { if (s) srand(s); }
inline long random(long howbig) { return howbig <= 0 ? 0 : rand() % howbig; }
inline long random(long lo, long hi) { return hi <= lo ? lo : lo + random(hi - lo); }

inline char* utoa(unsigned v, char* buf, int) { sprintf(buf, "%u", v); return buf; }
inline char* dtostrf(double v, signed char w, unsigned char p, char* buf) { sprintf(buf, "%*.*f", w, p, v); return buf; }

struct FakeSerial {
  void begin(long) {}
  template <class T> void print(T) {}
  template <class T> void print(T, int) {}
  template <class T> void println(T) {}
  template <class T> void println(T, int) {}
  void println() {}
};
static FakeSerial Serial;
