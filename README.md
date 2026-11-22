# Nano OLED Games

Small games for an **Arduino Nano** with a **1.3" 128x64 I2C OLED** module that has 4 buttons (^, v, #, *).

| Game | Folder | What it is |
| --- | --- | --- |
| Cave Flyer | [`cave_flyer/`](cave_flyer) | Fly through an endless scrolling cave that narrows and speeds up. Hold ^ or # to climb. |
| Kitty Pounce | [`kitty_pounce/`](kitty_pounce) | A cat guards three shelves: pounce on mice, dodge dogs, eat fish for extra hearts. |
| Pocket Kitty | [`pocket_kitty/`](pocket_kitty) | Virtual pet cat. Feed, play catch-the-yarn, nap and bathe Mochi. She reacts to your real room temperature (DHT11). Progress is saved. |
| Screen test | [`oled_test/`](oled_test) | I2C scan, display test patterns and a button tester. |
| DHT11 test | [`dht_test/`](dht_test) | Shows room temperature and humidity with min/max. |

## Screenshots

### Cave Flyer
<table>
<tr><td><img src="docs/screenshots/cave_title.png" width="256" alt="Cave Flyer title screen"></td><td><img src="docs/screenshots/cave_play.png" width="256" alt="Flying through the cave"></td><td><img src="docs/screenshots/cave_gameover.png" width="256" alt="Crashed screen with score"></td></tr>
</table>

### Kitty Pounce
<table>
<tr><td><img src="docs/screenshots/pounce_title.png" width="256" alt="Kitty Pounce title screen"></td><td><img src="docs/screenshots/pounce_play.png" width="256" alt="Cat catches a mouse while a dog charges"></td><td><img src="docs/screenshots/pounce_gameover.png" width="256" alt="Game over screen"></td></tr>
</table>

### Pocket Kitty
<table>
<tr><td><img src="docs/screenshots/kitty_home.png" width="256" alt="Mochi at home, snow in the window, 17C on the wall"></td><td><img src="docs/screenshots/kitty_feed.png" width="256" alt="Eating from the bowl"></td><td><img src="docs/screenshots/kitty_yarn.png" width="256" alt="Catch-the-yarn game"></td></tr>
<tr><td><img src="docs/screenshots/kitty_yarn_done.png" width="256" alt="Yarn game result"></td><td><img src="docs/screenshots/kitty_nap.png" width="256" alt="Napping under the moon"></td><td><img src="docs/screenshots/kitty_bath.png" width="256" alt="Bath time"></td></tr>
<tr><td><img src="docs/screenshots/kitty_info.png" width="256" alt="Needs, age, room reading and mood"></td><td><img src="docs/screenshots/dht_test.png" width="256" alt="DHT11 test sketch"></td><td></td></tr>
</table>

Mochi's room is cold (17 °C) in these shots, so it snows outside her window. The last image is the DHT11 test sketch.

These are rendered from the real sketches on a PC, not photographed: [`tools/screenshots/`](tools/screenshots)
runs each game with a fake Arduino core and an autopilot, and saves U8g2's screen buffer.
Run `tools/screenshots/build.sh` to regenerate them after changing a game.

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

DHT11 temperature/humidity sensor (3-pin module): DATA -> D2, VCC -> 5V, GND -> GND.
Only Pocket Kitty and the DHT11 test use it.

Optional battery gauge: battery divider (100k/100k) -> A1. See [Battery power](#battery-power).

Buttons connect to GND when pressed (the sketches use `INPUT_PULLUP`).

> Many 1.3" modules use the SH1106 controller instead. If the picture is shifted or has a noisy stripe,
> change `U8G2_SSD1306_128X64_NONAME_...` to `U8G2_SH1106_128X64_NONAME_...` at the top of the sketch.

## Battery power

The Nano needs a steady 5 V, but a Li-ion cell gives 3.0–4.2 V. So the battery goes through a charger and a
5 V boost converter:

**cell → TP4056 charger/protection → on/off switch → 5 V boost → Nano 5V pin**

The device draws about 40 mA at 5 V (Nano ~20–25 mA, OLED 10–20 mA, DHT11 <1 mA), which is about 64 mA
from the cell after boost losses.

### Parts for both versions

| Part | Notes |
| --- | --- |
| TP4056 USB-C charger module **with protection** (DW01A + 8205A chips, 6 pads: IN±, B±, OUT±) | Charges at 1 A by default (R_PROG = 1.2 kΩ) and cuts off at over-discharge or short circuit. |
| 5 V boost module, **fixed 5 V** output, ≥ 500 mA (e.g. Pololu U1V11F5) | An adjustable MT3608 also works, but **set it to 5.0 V with a multimeter first**: they ship at 12–28 V and would destroy the Nano. |
| Slide switch | On/off, between TP4056 OUT+ and the boost input. |
| 2 × 100 kΩ resistors | Voltage divider for Pocket Kitty's battery gauge (optional). |

Avoid all-in-one "power bank" boards (IP5306 and similar): they switch off automatically when the load is below
about 50 mA, and this device draws about 40 mA.

### Version 1: flat LiPo (small, about 15 hours)

| Part | Notes |
| --- | --- |
| 3.7 V 1S LiPo pouch, e.g. **503450, 1000 mAh**, with protection board and JST-PH 2.0 plug | Fits flat behind the OLED board. |

- The 1 A charge current is 1C for a 1000 mAh cell, which is fine. For a smaller cell (for example 500 mAh),
  replace R_PROG on the TP4056 with 2.4 kΩ (≈ 500 mA).
- **Check the JST plug polarity** before connecting: the red wire must go to B+. Some cells are wired the other way.

### Version 2: 18650 cell (bigger, about 40 hours)

| Part | Notes |
| --- | --- |
| One **18650 cell, 2500–3500 mAh**, from a known brand (Samsung, LG, Sony/Murata, Panasonic) | Avoid "9900 mAh" fakes. |
| Single-cell 18650 holder with wires | Or a cell with welded tabs. Never solder directly to the cell. |

The 1 A default charge current is fine for an 18650.

### Wiring

```
Battery +  -> TP4056 B+          TP4056 OUT+ -> switch --+--> boost VIN+
Battery -  -> TP4056 B-          TP4056 OUT- ------------+--> boost GND  -> Nano GND
                                                         |    boost 5V   -> Nano 5V pin
                                                         +-- 100k --+-- 100k -- GND
                                                                    +--> Nano A1
```

- **Charging:** plug USB-C into the TP4056 (red LED = charging, blue/green = full). You can keep playing while it
  charges; switching the device off lets the charger finish cleanly.
- **Uploading code** over the Nano's USB with the battery on is safe, but switch the battery off while uploading.
- **Never** connect the cell directly to the Nano's 5V or VIN pin. VIN needs 7 V or more.
- The divider sits after the switch, so it draws nothing when the device is off (about 20 µA when on).
- Optional: removing the Nano's power LED saves 3–5 mA, about 10% more runtime.

### Battery gauge in Pocket Kitty

With the divider wired to A1, Pocket Kitty shows a battery icon in the top-right corner and a battery bar on
the INFO screen. Below 15% Mochi says "low battery!", below 5% "charge me!", and she saves her progress
before the TP4056 cuts the power. If A1 is not wired, set `BATTERY_MONITOR` to `false` at the top of the sketch.
An unconnected A1 reads random values. If the percentage looks wrong, measure the Nano's 5V pin and put that
value in `VREF_MV`.

## Building

Requires the [U8g2](https://github.com/olikraus/u8g2) library. Pocket Kitty and the DHT11 test also need
Adafruit's "DHT sensor library" (and its dependency "Adafruit Unified Sensor").

With [arduino-cli](https://arduino.github.io/arduino-cli/):

```sh
arduino-cli core install arduino:avr
arduino-cli lib install U8g2 "DHT sensor library" "Adafruit Unified Sensor"
arduino-cli compile -b arduino:avr:nano pocket_kitty
arduino-cli upload  -b arduino:avr:nano -p /dev/ttyUSB0 pocket_kitty
```

Many Nano clones have the old bootloader: if upload fails with `not in sync`, use
`-b arduino:avr:nano:cpu=atmega328old`. In the Arduino IDE that is *Tools → Processor → ATmega328P (Old Bootloader)*.

## Notes

- Best scores and the pet's state are stored in EEPROM, each game in its own slot.
  - Cave Flyer / Kitty Pounce: hold **v** at power-on to reset the best score.
  - Pocket Kitty: hold **\*** at power-on to adopt a new kitty.
- Pocket Kitty room reactions: below 18 °C it snows outside the window and Mochi shivers and gets hungry faster;
  at 27 °C and up the sun shines and she sweats and tires faster. Change `COLD_C` / `HOT_C` to adjust.
- The Nano has only 2 KB of RAM. Pocket Kitty uses U8g2 page mode (`_2_` constructor) to save 768 bytes.
