# Nano OLED Games

Small games for an **Arduino Nano** with a **1.3" 128x64 I2C OLED** module that has 4 buttons (^, v, #, *).

| Game | Folder | What it is |
| --- | --- | --- |
| Cave Flyer | [`cave_flyer/`](cave_flyer) | Fly through an endless scrolling cave that narrows and speeds up. Hold ^ or # to climb. |
| Kitty Pounce | [`kitty_pounce/`](kitty_pounce) | A cat guards three shelves: pounce on mice, dodge dogs, eat fish for extra hearts. |
| Pocket Kitty | [`pocket_kitty/`](pocket_kitty) | Virtual pet cat. Feed, play catch-the-yarn, nap and bathe Mochi. Progress is saved. |
| Screen test | [`oled_test/`](oled_test) | I2C scan, display test patterns and a button tester. |

## Hardware

- Arduino Nano (ATmega328P)
- 1.3" 128x64 OLED, **SSD1306** controller, I2C address `0x3C`, with 4 push buttons

| OLED pin | Nano pin |
| --- | --- |
| GND | GND |
| VCC | 5V |
| SCL | A5 |
| SDA | A4 |
| K1 (^) | D12 |
| K2 (v) | D11 |
| K3 (#) | D10 |
| K4 (*) | D9 |

Buttons connect to GND when pressed (the sketches use `INPUT_PULLUP`).

> Many 1.3" modules use the SH1106 controller instead. If the picture is shifted or has a noisy stripe,
> change `U8G2_SSD1306_128X64_NONAME_...` to `U8G2_SH1106_128X64_NONAME_...` at the top of the sketch.

## Building

Requires the [U8g2](https://github.com/olikraus/u8g2) library.

With [arduino-cli](https://arduino.github.io/arduino-cli/):

```sh
arduino-cli core install arduino:avr
arduino-cli lib install U8g2
arduino-cli compile -b arduino:avr:nano pocket_kitty
arduino-cli upload  -b arduino:avr:nano -p /dev/ttyUSB0 pocket_kitty
```

Many Nano clones have the old bootloader: if upload fails with `not in sync`, use
`-b arduino:avr:nano:cpu=atmega328old`. In the Arduino IDE that is *Tools → Processor → ATmega328P (Old Bootloader)*.

## Notes

- Best scores and the pet's state are stored in EEPROM, each game in its own slot.
  - Cave Flyer / Kitty Pounce: hold **v** at power-on to reset the best score.
  - Pocket Kitty: hold **\*** at power-on to adopt a new kitty.
- The Nano has only 2 KB of RAM. Pocket Kitty uses U8g2 page mode (`_2_` constructor) to save 768 bytes.
