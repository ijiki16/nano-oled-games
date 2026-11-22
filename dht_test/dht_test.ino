/*
  DHT11 temperature & humidity test with the 1.3" SSD1306 OLED

  Wiring
    DHT11 DATA -> D2, VCC -> 5V, GND -> GND
    (a bare 4-pin DHT11 needs a 10k pull-up from DATA to VCC;
     3-pin modules on a small board already have it)
    OLED SDA -> A4, SCL -> A5

  Shows current temperature and humidity, plus min/max since power-on.
  Press # to reset min/max. Serial Monitor at 115200 shows each reading.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>

const uint8_t DHT_PIN = 2;
const uint8_t PIN_HASH = 10;

DHT dht(DHT_PIN, DHT11);
U8G2_SSD1306_128X64_NONAME_2_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

float tempC = NAN, hum = NAN;
float tMin = 99, tMax = -99, hMin = 99, hMax = -99;
uint16_t okReads = 0, badReads = 0;

void resetMinMax() { tMin = 99; tMax = -99; hMin = 99; hMax = -99; }

void readSensor() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    badReads++;
    Serial.println(F("DHT11 read failed - check wiring on D2"));
    return;
  }
  okReads++;
  tempC = t; hum = h;
  tMin = min(tMin, t); tMax = max(tMax, t);
  hMin = min(hMin, h); hMax = max(hMax, h);
  Serial.print(F("Temp: ")); Serial.print(t, 1);
  Serial.print(F(" C  Humidity: ")); Serial.print(h, 0);
  Serial.println(F(" %"));
}

void draw() {
  char buf[24];
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 7, "DHT11 on D2");
  if (badReads && !okReads) {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.drawStr(0, 32, "No sensor!");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 46, "check DATA -> D2,");
    u8g2.drawStr(0, 55, "VCC and GND");
    return;
  }
  if (isnan(tempC)) {
    u8g2.drawStr(0, 32, "reading...");
    return;
  }
  // big numbers
  u8g2.setFont(u8g2_font_logisoso20_tn);
  dtostrf(tempC, 4, 1, buf);
  u8g2.drawStr(0, 36, buf);
  dtostrf(hum, 3, 0, buf);
  u8g2.drawStr(74, 36, buf);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(50, 22, "\xb0" "C");
  u8g2.drawStr(116, 22, "%");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 45, "temp");
  u8g2.drawStr(74, 45, "humidity");

  // min / max
  u8g2.drawHLine(0, 48, 128);
  char a[6], b[6];
  dtostrf(tMin, 3, 0, a); dtostrf(tMax, 3, 0, b);
  snprintf(buf, sizeof(buf), "min%s max%s", a, b);
  u8g2.drawStr(0, 57, buf);
  dtostrf(hMin, 3, 0, a); dtostrf(hMax, 3, 0, b);
  snprintf(buf, sizeof(buf), "%s-%s%%", a, b);
  u8g2.drawStr(80, 57, buf);
  u8g2.drawStr(0, 64, "# resets min/max");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_HASH, INPUT_PULLUP);
  dht.begin();
  u8g2.setBusClock(400000);
  u8g2.begin();
  Serial.println(F("DHT11 test"));
}

void loop() {
  static uint32_t lastRead = 0;
  // DHT11 can only be read about once a second; every 2 s is safe
  if (millis() - lastRead >= 2000 || lastRead == 0) {
    lastRead = millis();
    readSensor();
  }
  if (digitalRead(PIN_HASH) == LOW && !isnan(tempC)) {
    resetMinMax();
    tMin = tMax = tempC; hMin = hMax = hum;
  }
  u8g2.firstPage();
  do { draw(); } while (u8g2.nextPage());
  delay(50);
}
