#pragma once
#include <string.h>
#include <stdint.h>
extern uint8_t g_eeprom[1024];
struct FakeEEPROM {
  uint8_t read(int a) { return g_eeprom[a]; }
  void write(int a, uint8_t v) { g_eeprom[a] = v; }
  void update(int a, uint8_t v) { g_eeprom[a] = v; }
  template <class T> T& get(int a, T& t) { memcpy(&t, g_eeprom + a, sizeof(T)); return t; }
  template <class T> const T& put(int a, const T& t) { memcpy(g_eeprom + a, &t, sizeof(T)); return t; }
};
static FakeEEPROM EEPROM;
