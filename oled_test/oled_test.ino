/*
  OLED + 4-button module test for Arduino Nano (ATmega328P)

  Module: 1.3" 128x64 I2C OLED with 4 keys (^, v, #, *)

  WIRING
    OLED GND -> Nano GND
    OLED VCC -> Nano 5V
    OLED SCL -> Nano A5
    OLED SDA -> Nano A4
    OLED K1  -> Nano D12  (^ up)
    OLED K2  -> Nano D11  (v down)
    OLED K3  -> Nano D10  (#)
    OLED K4  -> Nano D9   (*)

  LIBRARY
    Install "U8g2" by oliver in Arduino IDE: Tools > Manage Libraries.

  CONTROLLER
    1.3" modules almost always use the SH1106 chip; 0.96" ones use SSD1306.
    If the image is shifted, has a noisy stripe on one side, or is blank,
    comment out the SH1106 line and use the SSD1306 line instead.

  Open Serial Monitor at 115200 baud to see the I2C scan and button events.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const uint8_t BTN_PINS[4]  = {12, 11, 10, 9};
const char*   BTN_NAMES[4] = {"UP", "DOWN", "#", "*"};

uint8_t i2cAddr = 0;

void scanI2C() {
  Serial.println(F("Scanning I2C bus..."));
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  Found device at 0x"));
      Serial.println(a, HEX);
      if (!i2cAddr) i2cAddr = a;
    }
  }
  if (!i2cAddr) Serial.println(F("  No I2C devices found - check wiring!"));
}

// --- Test screens ---------------------------------------------------------

void testFill() {
  u8g2.clearBuffer();
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.sendBuffer();
  delay(800);
}

void testBorderAndText() {
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);  // all 4 edges should be visible
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(18, 26, "OLED TEST");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(22, 44, "128 x 64 pixels");
  char buf[20];
  snprintf(buf, sizeof(buf), "I2C addr: 0x%02X", i2cAddr);
  u8g2.drawStr(22, 56, buf);
  u8g2.sendBuffer();
  delay(2000);
}

void testShapes() {
  u8g2.clearBuffer();
  u8g2.drawCircle(20, 32, 16);
  u8g2.drawDisc(55, 32, 12);
  u8g2.drawTriangle(75, 50, 95, 14, 115, 50);
  for (int x = 0; x < 128; x += 8) u8g2.drawPixel(x, 62);
  u8g2.sendBuffer();
  delay(1500);
}

void testCheckerboard() {
  u8g2.clearBuffer();
  for (int y = 0; y < 64; y += 8)
    for (int x = 0; x < 128; x += 8)
      if (((x + y) / 8) % 2 == 0) u8g2.drawBox(x, y, 8, 8);
  u8g2.sendBuffer();
  delay(1500);
}

void testContrast() {
  u8g2.clearBuffer();
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.sendBuffer();
  for (int c = 255; c >= 0; c -= 5) { u8g2.setContrast(c); delay(10); }
  for (int c = 0; c <= 255; c += 5) { u8g2.setContrast(c); delay(10); }
  u8g2.setContrast(255);
}

void testBouncingBall() {
  int x = 10, y = 10, dx = 2, dy = 1;
  for (int i = 0; i < 150; i++) {
    u8g2.clearBuffer();
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawDisc(x, y, 4);
    u8g2.sendBuffer();
    x += dx; y += dy;
    if (x <= 5 || x >= 122) dx = -dx;
    if (y <= 5 || y >= 58)  dy = -dy;
  }
}

// --- Interactive button test ---------------------------------------------

void drawButtonScreen(int cursor, int pressCount[]) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 9, "Button test");
  u8g2.drawHLine(0, 11, 128);

  for (int i = 0; i < 4; i++) {
    bool down = digitalRead(BTN_PINS[i]) == LOW;
    int bx = 4 + i * 31;
    if (down) u8g2.drawBox(bx, 16, 27, 14);
    else      u8g2.drawFrame(bx, 16, 27, 14);
    u8g2.setDrawColor(down ? 0 : 1);
    u8g2.drawStr(bx + 14 - strlen(BTN_NAMES[i]) * 3, 27, BTN_NAMES[i]);
    u8g2.setDrawColor(1);

    char n[6];
    snprintf(n, sizeof(n), "%d", pressCount[i]);
    u8g2.drawStr(bx + 14 - strlen(n) * 3, 42, n);
  }

  // UP/DOWN move a slider, # resets counters, * re-runs the demo
  u8g2.drawFrame(4, 50, 120, 10);
  u8g2.drawBox(4, 50, cursor * 120 / 20, 10);
  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < 4; i++) pinMode(BTN_PINS[i], INPUT_PULLUP);

  Wire.begin();
  scanI2C();

  u8g2.setBusClock(400000);  // fast I2C = smoother animation
  u8g2.begin();
  Serial.println(F("Running display tests..."));
  testFill();
  testBorderAndText();
  testShapes();
  testCheckerboard();
  testContrast();
  testBouncingBall();
  Serial.println(F("Done. Press buttons: UP/DOWN = slider, # = reset counts, * = rerun demo"));
}

void loop() {
  static int cursor = 10;
  static int pressCount[4] = {0, 0, 0, 0};
  static bool last[4] = {false, false, false, false};

  for (int i = 0; i < 4; i++) {
    bool down = digitalRead(BTN_PINS[i]) == LOW;
    if (down && !last[i]) {          // new press
      pressCount[i]++;
      Serial.print(F("Pressed: "));
      Serial.println(BTN_NAMES[i]);
      if (i == 0 && cursor < 20) cursor++;
      if (i == 1 && cursor > 0)  cursor--;
      if (i == 2) for (int j = 0; j < 4; j++) pressCount[j] = 0;
      if (i == 3) { testBorderAndText(); testShapes(); testBouncingBall(); }
    }
    last[i] = down;
  }

  drawButtonScreen(cursor, pressCount);
  delay(20);  // simple debounce + ~50 fps
}
