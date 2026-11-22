"""Draws the device schematics as SVG (and PNG via ImageMagick).

Power: 1000 mAh LiPo on an Adafruit PowerBoost 500 Charger (charger and 5 V boost in one board).

Usage: python3 tools/schematic/make_schematics.py      (from the repo root)
Output: docs/schematics/schematic.svg and schematic.png
"""
import pathlib
import shutil
import subprocess
from xml.sax.saxutils import escape

W, H = 1300, 940
WIRE, BODY, FILL, NET, PWR, TXT, MUTED = "#0f766e", "#7f1d1d", "#fffbea", "#1d4ed8", "#b91c1c", "#111827", "#6b7280"
FONT = "Noto Sans, DejaVu Sans, Arial, sans-serif"
MONO = "Noto Sans Mono, DejaVu Sans Mono, monospace"


class Sheet:
    def __init__(self):
        self.parts = []

    def add(self, s):
        self.parts.append(s)

    # ---- primitives -------------------------------------------------------------
    def wire(self, *pts):
        p = " ".join(f"{x},{y}" for x, y in pts)
        self.add(f'<polyline points="{p}" fill="none" stroke="{WIRE}" stroke-width="2"/>')

    def line(self, x1, y1, x2, y2, color=BODY, w=2, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{w}"{d}/>')

    def dot(self, x, y):
        self.add(f'<circle cx="{x}" cy="{y}" r="4" fill="{WIRE}"/>')

    def text(self, x, y, s, size=13, anchor="start", weight="normal", color=TXT, font=FONT):
        self.add(f'<text x="{x}" y="{y}" font-family="{font}" font-size="{size}" text-anchor="{anchor}" '
                 f'font-weight="{weight}" fill="{color}">{escape(s)}</text>')

    def rect(self, x, y, w, h, stroke=BODY, fill=FILL, sw=2, dash=None, r=0):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="{sw}"{d}/>')

    # ---- symbols ----------------------------------------------------------------
    def block(self, x, y, w, h, ref, lines, left=(), right=(), top=()):
        """Module box. Pins are (name, y) on the sides or (name, x) on top; stubs are 20 px."""
        self.rect(x, y, w, h)
        self.text(x, y - 8, ref, 13, weight="bold", color=BODY)
        for i, s in enumerate(lines):
            self.text(x + w / 2, y + 22 + i * 16, s, 13 if i == 0 else 11,
                      anchor="middle", weight="bold" if i == 0 else "normal", color=TXT if i == 0 else MUTED)
        for name, py in left:
            self.line(x - 20, py, x, py)
            self.text(x + 6, py + 4, name, 12, font=MONO)
        for name, py in right:
            self.line(x + w, py, x + w + 20, py)
            self.text(x + w - 6, py + 4, name, 12, anchor="end", font=MONO)
        for name, px in top:
            self.line(px, y - 20, px, y)
            self.text(px, y + 14 + 16 * len(lines), name, 12, anchor="middle", font=MONO)

    def gnd(self, x, y):
        self.line(x, y, x, y + 12, PWR)
        for i, half in enumerate((11, 7, 3)):
            self.line(x - half, y + 12 + i * 4, x + half, y + 12 + i * 4, PWR)

    def vcc(self, x, y, label="+5V"):
        self.line(x, y, x, y - 14, PWR)
        self.line(x - 10, y - 14, x + 10, y - 14, PWR, 3)
        self.text(x, y - 20, label, 12, anchor="middle", weight="bold", color=PWR)

    def label(self, x, y, name, side):
        """Net label flag whose point touches (x, y); side = 'left' or 'right' of the point."""
        w = len(name) * 7.5 + 10
        s = -1 if side == "left" else 1
        pts = [(x, y), (x + s * 8, y - 9), (x + s * (8 + w), y - 9), (x + s * (8 + w), y + 9), (x + s * 8, y + 9)]
        p = " ".join(f"{a},{b}" for a, b in pts)
        self.add(f'<polygon points="{p}" fill="#eff6ff" stroke="{NET}" stroke-width="1.5"/>')
        self.text(x + s * (8 + w / 2), y + 4, name, 12, anchor="middle", weight="bold", color=NET, font=MONO)

    def resistor(self, x, y1, y2, ref, value):
        self.wire((x, y1), (x, y1 + 8))
        self.rect(x - 7, y1 + 8, 14, y2 - y1 - 16, fill="white")
        self.wire((x, y2 - 8), (x, y2))
        self.text(x + 14, (y1 + y2) / 2 - 2, ref, 12, weight="bold", color=BODY)
        self.text(x + 14, (y1 + y2) / 2 + 13, value, 12, color=MUTED)

    def switch(self, xa, xb, y, ref, value):
        self.add(f'<circle cx="{xa}" cy="{y}" r="3.5" fill="white" stroke="{BODY}" stroke-width="2"/>')
        self.add(f'<circle cx="{xb}" cy="{y}" r="3.5" fill="white" stroke="{BODY}" stroke-width="2"/>')
        self.line(xa + 3, y - 2, xb - 4, y - 16)
        self.text((xa + xb) / 2, y - 26, ref, 12, anchor="middle", weight="bold", color=BODY)
        self.text((xa + xb) / 2, y + 22, value, 11, anchor="middle", color=MUTED)

    def button(self, xa, xb, y):
        """Normally-open push button between xa and xb."""
        self.add(f'<circle cx="{xa}" cy="{y}" r="3" fill="white" stroke="{BODY}" stroke-width="1.8"/>')
        self.add(f'<circle cx="{xb}" cy="{y}" r="3" fill="white" stroke="{BODY}" stroke-width="1.8"/>')
        self.line(xa - 2, y - 8, xb + 2, y - 8, BODY, 1.8)
        self.line((xa + xb) / 2, y - 8, (xa + xb) / 2, y - 14, BODY, 1.8)

    def battery(self, x, y_top, y_bot, ref, lines):
        mid = (y_top + y_bot) / 2
        self.wire((x, y_top), (x, mid - 14))
        for i, (half, wgt) in enumerate(((16, 2), (8, 4), (16, 2), (8, 4))):
            self.line(x - half, mid - 14 + i * 9, x + half, mid - 14 + i * 9, BODY, wgt)
        self.wire((x, mid + 13), (x, y_bot))
        self.text(x - 26, mid - 14, "+", 16, weight="bold", color=BODY)
        self.text(x - 52, y_top - 12, ref, 13, weight="bold", color=BODY)
        for i, s in enumerate(lines):
            self.text(x - 52, y_bot + 22 + i * 15, s, 11, color=MUTED)

    def svg(self):
        body = "\n".join(self.parts)
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n'
                f'<rect width="{W}" height="{H}" fill="white"/>\n{body}\n</svg>\n')


def power_powerboost(s):
    """Adafruit PowerBoost 500 Charger = charger + 5 V boost in one board."""
    yb, yg = 210, 290
    s.battery(110, yb, yg, "BT1", ["LiPo 3.7 V 1000 mAh", "JST-PH 2-pin plug", "check +/- marks"])
    s.wire((110, yb), (280, yb))
    s.wire((110, yg), (280, yg))
    # BAT net also feeds the gauge divider (drawn on the right, joined by the VBAT label)
    s.dot(200, yb)
    s.wire((200, yb), (200, 160))
    s.label(200, 160, "VBAT", "right")

    s.block(300, 130, 200, 220, "U2", ["PowerBoost 500 Charger", "Adafruit #1944", "charger + 5 V boost"],
            left=[("BAT", yb), ("GND", yg)],
            right=[("5V", 210), ("GND", 250), ("EN", 290), ("LBO", 330)])
    s.rect(352, 86, 96, 26, fill="white")
    s.text(400, 104, "micro-USB in", 12, anchor="middle", font=MONO)
    s.line(400, 112, 400, 130)
    s.text(460, 104, "charges at 500 mA, runs while charging", 11, color=MUTED)
    s.text(300, 370, "BAT/GND = the board's JST-PH 2-pin connector", 11, color=MUTED)

    s.wire((520, 210), (600, 210))
    s.vcc(600, 210)
    s.wire((520, 250), (540, 250))
    s.gnd(540, 250)
    # EN is pulled high to BAT: a small switch to GND turns the 5 V output off
    s.wire((520, 290), (560, 290))
    s.switch(564, 620, 290, "SW1", "closed = off")
    s.wire((624, 290), (660, 290))
    s.gnd(660, 290)
    # LBO drops to 0 V below 3.2 V (also lights the board's red LED)
    s.wire((520, 330), (560, 330))
    s.label(560, 330, "LOW_BAT", "right")

    # Battery gauge divider on VBAT
    s.wire((780, 170), (780, 190))
    s.label(780, 170, "VBAT", "left")
    s.resistor(780, 190, 250, "R1", "100k")
    s.dot(780, 265)
    s.wire((780, 250), (780, 280))
    s.wire((780, 265), (745, 265))
    s.label(745, 265, "BAT_SENSE", "left")
    s.resistor(780, 280, 340, "R2", "100k")
    s.gnd(780, 340)
    s.text(700, 386, "R1, R2: battery gauge (optional);", 11, color=MUTED)
    s.text(700, 400, "on VBAT they draw ≈ 20 µA even when off", 11, color=MUTED)

    notes = [
        ("U1", "Arduino Nano (ATmega328P, 5 V)"),
        ("U2", "Adafruit PowerBoost 500 Charger"),
        ("U4", "DHT11, 3-pin module (pull-up on board)"),
        ("U5", "1.3\" OLED 128x64 SSD1306 + 4 keys"),
        ("BT1", "LiPo 1000 mAh, JST-PH 2-pin"),
        ("SW1", "small switch, EN to GND"),
        ("BZ1", "passive piezo buzzer (optional)"),
        ("R3", "100 Ω (buzzer)"),
        ("R1,R2", "100 kΩ (battery gauge)"),
    ]
    tips = ["Runtime ≈ 15 h (1000 mAh LiPo)",
            "Charging is micro-USB, not USB-C.",
            "Off = EN low: 5 V output disconnected,",
            "  board draws ≈ 20 µA.",
            "LOW_BAT (LBO) goes low below 3.2 V.",
            "Never wire the cell to Nano 5V or VIN."]
    return notes, tips


def draw():
    s = Sheet()
    s.text(40, 34, "Nano OLED Games — schematic (Adafruit PowerBoost 500 Charger)", 20, weight="bold")
    s.text(W - 40, 34, "github.com/ijiki16/nano-oled-games · 2026-11-22", 12, anchor="end", color=MUTED)

    # ======================= power and charging ======================================
    s.rect(30, 56, 900, 380, stroke=MUTED, fill="none", sw=1.2, dash="6 5", r=6)
    s.text(44, 76, "POWER AND CHARGING", 12, weight="bold", color=MUTED)
    notes, tips = power_powerboost(s)

    # ======================= parts notes ===============================================
    s.rect(950, 56, 320, 380, stroke=MUTED, fill="#f9fafb", sw=1.2, r=6)
    s.text(966, 80, "PARTS", 12, weight="bold", color=MUTED)
    for i, (ref, desc) in enumerate(notes):
        s.text(966, 106 + i * 20, ref, 12, weight="bold", color=BODY, font=MONO)
        s.text(1024, 106 + i * 20, desc, 12)
    for i, t in enumerate(tips):
        s.text(966, 304 + i * 18, t, 12, color=TXT if i == 0 else MUTED, weight="bold" if i == 0 else "normal")

    # ======================= logic ======================================================
    s.rect(30, 456, 1240, 466, stroke=MUTED, fill="none", sw=1.2, dash="6 5", r=6)
    s.text(44, 476, "LOGIC", 12, weight="bold", color=MUTED)

    # U1 Nano
    nx, ny = 520, 500
    s.block(nx, ny, 180, 380, "U1", ["Arduino Nano", "ATmega328P", "USB: programming"],
            left=[("D2", 580), ("D3", 620),
                  ("D9", 680), ("D10", 720), ("D11", 760), ("D12", 800), ("D5", 840)],
            right=[("GND", 540), ("5V", 580), ("A5/SCL", 640), ("A4/SDA", 680), ("A1", 800)])
    # optional: low-battery signal from the PowerBoost (all games show a warning)
    s.wire((nx - 20, 620), (nx - 40, 620))
    s.label(nx - 40, 620, "LOW_BAT", "left")
    s.text(nx - 40, 646, "optional", 11, anchor="end", color=MUTED)
    for pin_y, net in ((680, "K4"), (720, "K3"), (760, "K2"), (800, "K1")):
        s.wire((nx - 20, pin_y), (nx - 40, pin_y))
        s.label(nx - 40, pin_y, net, "left")
    # optional buzzer for sound effects
    s.wire((nx - 20, 840), (nx - 40, 840))
    s.label(nx - 40, 840, "BUZZER", "left")
    s.label(250, 720, "BUZZER", "left")
    s.wire((250, 720), (290, 720))
    s.resistor(290, 720, 780, "R3", "100 Ω")
    s.wire((290, 780), (290, 792))
    s.rect(276, 792, 28, 22, fill="white")
    s.add('<circle cx="290" cy="803" r="7" fill="none" stroke="#7f1d1d" stroke-width="2"/>')
    s.wire((290, 814), (290, 830))
    s.gnd(290, 830)
    s.text(316, 802, "BZ1", 12, weight="bold", color=BODY)
    s.text(316, 817, "passive piezo (optional)", 11, color=MUTED)

    # Nano power
    s.wire((720, 540), (750, 540))
    s.gnd(750, 540)
    s.wire((720, 580), (790, 580))
    s.vcc(790, 580)
    # I2C straight across to the OLED module
    s.wire((720, 640), (940, 640))
    s.wire((720, 680), (940, 680))
    s.text(830, 634, "SCL", 11, anchor="middle", color=MUTED, font=MONO)
    s.text(830, 674, "SDA", 11, anchor="middle", color=MUTED, font=MONO)
    s.wire((720, 800), (760, 800))
    s.label(760, 800, "BAT_SENSE", "right")
    s.text(760, 782, "optional gauge", 11, color=MUTED)

    # U4 DHT11
    s.block(140, 520, 190, 130, "U4", ["DHT11", "3-pin module", "pull-up on board"],
            right=[("VCC", 540), ("DATA", 560 + 20), ("GND", 620)])
    s.wire((350, 540), (380, 540))
    s.vcc(380, 540)
    s.wire((350, 580), (500, 580))
    s.text(415, 572, "DHT_DATA", 11, anchor="middle", color=MUTED, font=MONO)
    s.wire((350, 620), (380, 620))
    s.gnd(380, 620)

    # U5 OLED module with its 4 keys (each key pulls its pin to GND)
    ox = 960
    s.block(ox, 500, 260, 400, "U5", ["OLED module", "1.3\" 128x64 SSD1306", "I2C address 0x3C"],
            left=[("GND", 540), ("VCC", 580), ("SCL", 640), ("SDA", 680),
                  ("K4", 740), ("K3", 780), ("K2", 820), ("K1", 860)])
    s.wire((940, 540), (910, 540))
    s.gnd(910, 540)
    s.wire((940, 580), (880, 580))
    s.vcc(880, 580)
    keys = ((740, "K4", "*"), (780, "K3", "#"), (820, "K2", "v"), (860, "K1", "^"))
    rail = ox + 140
    for ky, net, sym in keys:
        s.line(ox + 36, ky, ox + 80, ky, BODY, 1.8)
        s.button(ox + 84, ox + 124, ky)
        s.line(ox + 127, ky, rail, ky, BODY, 1.8)
        s.text(ox + 104, ky + 17, f"key {sym}", 10, anchor="middle", color=MUTED)
        s.wire((ox - 20, ky), (ox - 30, ky))
        s.label(ox - 30, ky, net, "left")
    s.line(rail, 740, rail, 872, BODY, 1.8)
    s.gnd(rail, 872)
    s.text(rail + 14, 760, "keys pull", 10, color=MUTED)
    s.text(rail + 14, 773, "pin to GND", 10, color=MUTED)
    s.text(rail + 14, 786, "(INPUT_PULLUP)", 10, color=MUTED)

    # Net label legend
    s.text(44, 900, f"Same name = connected: K1–K4 (keys), BAT_SENSE (gauge), VBAT, LOW_BAT, BUZZER, +5V and GND symbols.",
           12, color=MUTED)
    return s.svg()


def main():
    repo = pathlib.Path(__file__).resolve().parents[2]
    out = repo / "docs" / "schematics"
    out.mkdir(parents=True, exist_ok=True)
    svg = out / "schematic.svg"
    svg.write_text(draw())
    print("wrote", svg)
    if shutil.which("magick"):
        png = out / "schematic.png"
        subprocess.run(["magick", "-density", "96", str(svg), str(png)], check=True)
        print("wrote", png)


if __name__ == "__main__":
    main()
