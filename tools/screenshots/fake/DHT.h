#pragma once
#include <stdint.h>
#define DHT11 11
extern float g_tempC, g_hum;
class DHT {
 public:
  DHT(uint8_t, uint8_t) {}
  void begin() {}
  float readTemperature() { return g_tempC; }
  float readHumidity() { return g_hum; }
};
