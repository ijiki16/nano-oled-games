"""Draws the device schematics as SVG (and PNG via ImageMagick).

One schematic per battery version; everything else is the same circuit:
  Version 1: flat LiPo pouch     Version 2: 18650 cell

Usage: python3 tools/schematic/make_schematics.py      (from the repo root)
Output: docs/schematics/schematic_v1_lipo.svg/.png, schematic_v2_18650.svg/.png
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


def draw(version):
    v1 = version == 1
    s = Sheet()
    title = "Version 1: flat LiPo" if v1 else "Version 2: 18650 cell"
    s.text(40, 34, f"Nano OLED Games — schematic, {title}", 20, weight="bold")
    s.text(W - 40, 34, "github.com/ijiki16/nano-oled-games · 2026-11-22", 12, anchor="end", color=MUTED)

    # ======================= power and charging ======================================
    s.rect(30, 56, 900, 380, stroke=MUTED, fill="none", sw=1.2, dash="6 5", r=6)
    s.text(44, 76, "POWER AND CHARGING", 12, weight="bold", color=MUTED)

    # BT1 -> TP4056 (battery minus goes to B-, NOT to ground: the protection switch sits in between)
    yp, ym = 210, 260
    s.battery(110, yp, ym, "BT1", ["LiPo 503450", "3.7 V 1000 mAh", "JST-PH: red = B+"] if v1
              else ["18650 cell", "3.7 V 2500–3500 mAh", "in single-cell holder"])
    s.wire((110, yp), (210, yp))
    s.wire((110, ym), (210, ym))

    s.block(230, 130, 150, 150, "U2", ["TP4056", "USB-C charger", "+ protection (DW01A)"],
            left=[("B+", yp), ("B-", ym)], right=[("OUT+", yp), ("OUT-", ym)])
    # USB-C charging connector on top
    s.rect(262, 86, 86, 26, fill="white")
    s.text(305, 104, "USB-C in", 12, anchor="middle", font=MONO)
    s.line(305, 112, 305, 130)
    s.text(360, 104, "charge: red LED on, blue/green = full", 11, color=MUTED)
    s.wire((400, ym), (420, ym))
    s.gnd(420, ym)

    # SW1, then the switched battery line feeds the boost converter and the gauge divider
    s.wire((400, yp), (440, yp))
    s.switch(444, 500, yp, "SW1", "on/off")
    s.wire((504, yp), (580, yp))
    s.dot(580, yp)
    s.wire((580, yp), (660, yp))
    s.resistor(580, yp, yp + 60, "R1", "100k")
    s.dot(580, yp + 75)
    s.wire((580, yp + 60), (580, yp + 90))
    s.wire((580, yp + 75), (545, yp + 75))
    s.label(545, yp + 75, "BAT_SENSE", "left")
    s.resistor(580, yp + 90, yp + 150, "R2", "100k")
    s.gnd(580, yp + 150)
    s.text(600, yp + 180, "R1, R2: battery gauge (optional)", 11, color=MUTED)

    s.block(680, 130, 150, 150, "U3", ["5 V boost", "fixed 5 V, ≥ 500 mA", "(MT3608: set 5.0 V first!)"],
            left=[("VIN", yp), ("GND", ym)], right=[("VOUT", yp), ("GND", ym)])
    s.wire((660, ym), (650, ym))
    s.gnd(650, ym)
    s.wire((850, ym), (870, ym))
    s.gnd(870, ym)
    s.wire((850, yp), (890, yp))
    s.vcc(890, yp)

    # ======================= parts notes ===============================================
    s.rect(950, 56, 320, 380, stroke=MUTED, fill="#f9fafb", sw=1.2, r=6)
    s.text(966, 80, "PARTS", 12, weight="bold", color=MUTED)
    notes = [
        ("U1", "Arduino Nano (ATmega328P, 5 V)"),
        ("U2", "TP4056 USB-C module with protection"),
        ("U3", "5 V boost, e.g. Pololu U1V11F5"),
        ("U4", "DHT11, 3-pin module (pull-up on board)"),
        ("U5", "1.3\" OLED 128x64 SSD1306 + 4 keys"),
        ("BT1", "LiPo 503450 1000 mAh" if v1 else "18650 2500–3500 mAh"),
        ("SW1", "slide switch"),
        ("R1,R2", "100 kΩ (battery gauge)"),
    ]
    for i, (ref, desc) in enumerate(notes):
        s.text(966, 106 + i * 20, ref, 12, weight="bold", color=BODY, font=MONO)
        s.text(1024, 106 + i * 20, desc, 12)
    tips = ([f"Runtime ≈ {'15' if v1 else '40'} h (≈ 40 mA at 5 V)",
             "Never wire the cell to Nano 5V or VIN.",
             "Avoid IP5306 power-bank boards:",
             "  they switch off below ≈ 50 mA.",
             "Program over the Nano's own USB;",
             "  switch SW1 off while uploading."])
    for i, t in enumerate(tips):
        s.text(966, 284 + i * 18, t, 12, color=TXT if i == 0 else MUTED, weight="bold" if i == 0 else "normal")

    # ======================= logic ======================================================
    s.rect(30, 456, 1240, 466, stroke=MUTED, fill="none", sw=1.2, dash="6 5", r=6)
    s.text(44, 476, "LOGIC", 12, weight="bold", color=MUTED)

    # U1 Nano
    nx, ny = 520, 500
    s.block(nx, ny, 180, 380, "U1", ["Arduino Nano", "ATmega328P", "USB: programming"],
            left=[("D2", 580), ("D9", 680), ("D10", 720), ("D11", 760), ("D12", 800)],
            right=[("GND", 540), ("5V", 580), ("A5/SCL", 640), ("A4/SDA", 680), ("A1", 800)])
    for pin_y, net in ((680, "K4"), (720, "K3"), (760, "K2"), (800, "K1")):
        s.wire((nx - 20, pin_y), (nx - 40, pin_y))
        s.label(nx - 40, pin_y, net, "left")
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
    s.text(44, 900, "Same name = connected: K1–K4 (keys), BAT_SENSE (gauge), +5V and GND symbols.", 12, color=MUTED)
    return s.svg()


def main():
    repo = pathlib.Path(__file__).resolve().parents[2]
    out = repo / "docs" / "schematics"
    out.mkdir(parents=True, exist_ok=True)
    for version, name in ((1, "schematic_v1_lipo"), (2, "schematic_v2_18650")):
        svg = out / f"{name}.svg"
        svg.write_text(draw(version))
        print("wrote", svg)
        if shutil.which("magick"):
            png = out / f"{name}.png"
            subprocess.run(["magick", "-density", "96", str(svg), str(png)], check=True)
            print("wrote", png)


if __name__ == "__main__":
    main()
