// Power: sleep after idle (screen off, chip in power-down, any key wakes it)
// and the PowerBoost 500 Charger's low-battery signal.
//   Wiring: PowerBoost LBO -> D3 (optional). LBO drops to 0 V below 3.2 V.
//   D3 uses the internal pull-up, so an unwired D3 simply reads "battery OK".
#pragma once
#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include "Keys.h"
#include "Sfx.h"

const uint8_t PIN_LBO = 3;

// Any key change on PB1..PB4 (D9..D12) fires this; it only has to wake the chip.
ISR(PCINT0_vect) {}

inline void powerBegin() { pinMode(PIN_LBO, INPUT_PULLUP); }

// True once LBO has read LOW for 3 checks in a row, one check per second.
inline bool lowBattery() {
  static uint32_t next = 0;
  static uint8_t lowCount = 0;
  uint32_t t = millis();
  if ((int32_t)(t - next) >= 0) {
    next = t + 1000;
    if (digitalRead(PIN_LBO) == LOW) { if (lowCount < 3) lowCount++; }
    else lowCount = 0;
  }
  return lowCount >= 3;
}

// Screen off and power-down sleep until a key is pressed. millis() stands still
// while asleep. The key that wakes the device is swallowed.
template <class D> void powerSleep(D& display) {
  sfxStop();
  display.setPowerSave(1);
  const uint8_t KEY_BITS = 0x1E;               // PB1..PB4 = D9..D12
  while ((PINB & KEY_BITS) != KEY_BITS) {}     // wait until every key is up
  uint8_t adc = ADCSRA;
  ADCSRA = 0;                                  // ADC off saves ~0.3 mA
  PCMSK0 = KEY_BITS;
  PCIFR = _BV(PCIF0);
  PCICR |= _BV(PCIE0);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_enable();
  sei();
  sleep_cpu();                                 // ...zzz, until a key goes down
  sleep_disable();
  PCICR &= ~_BV(PCIE0);
  ADCSRA = adc;
  display.setPowerSave(0);
  keysSwallow();
}

// Sleep if no key was touched for idleLimitMs. Returns true after waking up.
template <class D> bool powerIdle(D& display, uint32_t idleLimitMs) {
  if (idleMs() < idleLimitMs) return false;
  powerSleep(display);
  return true;
}

// 9x5 battery outline, drawn empty: for the low-battery warning.
template <class D> void drawLowBattery(D& d, uint8_t x, uint8_t y) {
  d.drawFrame(x, y, 9, 5);
  d.drawVLine(x + 9, y + 1, 3);
}
