# Draws the pictures of MeteoWifi for the 128x64 screen and writes src/art.h.
# Plain Python, no libraries: circles, thick strokes and polygons on a grid of pixels.
#   python make_art.py            writes the header
#   python make_art.py --show     also prints every picture as text, to look at it
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, "..", "src", "art.h")
ICON = 48


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[False] * w for _ in range(h)]

    def put(self, x, y, on=True):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = on

    def shape(self, inside, on=True):
        for y in range(self.h):
            for x in range(self.w):
                if inside(x + 0.5, y + 0.5):
                    self.px[y][x] = on

    def disc(self, cx, cy, r, on=True):
        self.shape(lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r, on)

    def ring(self, cx, cy, r, t, on=True):
        self.shape(lambda x, y: (r - t) ** 2 <= (x - cx) ** 2 + (y - cy) ** 2 <= r * r, on)

    def stroke(self, x0, y0, x1, y1, t, on=True):
        def near(x, y):
            dx, dy = x1 - x0, y1 - y0
            k = 0 if dx == dy == 0 else max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)))
            return math.hypot(x - (x0 + k * dx), y - (y0 + k * dy)) <= t / 2
        self.shape(near, on)

    def polygon(self, pts, on=True):
        def inside(x, y):
            hit = False
            for i in range(len(pts)):
                (ax, ay), (bx, by) = pts[i], pts[i - 1]
                if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay) + ax:
                    hit = not hit
            return hit
        self.shape(inside, on)


# ---------------------------------------------------------------- icons

def cloud_mask(cx, cy, s, grow=0.0):
    """A cloud as a union of circles on a flat base; s scales it, grow widens it."""
    parts = [(-9, 2, 7), (0, -4, 10), (10, 1, 8)]
    base = (-16, 2, 18, 10)   # left, top, right, bottom of the flat part
    def inside(x, y):
        for dx, dy, r in parts:
            if (x - cx - dx * s) ** 2 + (y - cy - dy * s) ** 2 <= (r * s + grow) ** 2:
                return True
        l, t, r_, b = base
        return (cx + l * s - grow <= x <= cx + r_ * s + grow and
                cy + t * s - grow <= y <= cy + b * s + grow)
    return inside


def cloud(c, cx, cy, s=1.0, t=3):
    """Outline of a cloud, with what is inside cleared."""
    outer = cloud_mask(cx, cy, s)
    around = [(math.cos(i * math.pi / 8) * t, math.sin(i * math.pi / 8) * t) for i in range(16)]
    # Inside: every point within the thickness of the line is still part of the cloud.
    inner = lambda x, y: all(outer(x + dx, y + dy) for dx, dy in around)
    c.shape(outer, False)
    c.shape(lambda x, y: outer(x, y) and not inner(x, y))


def sun(c, cx, cy, r, rays=True):
    c.disc(cx, cy, r)
    if rays:
        for i in range(8):
            a = i * math.pi / 4
            c.stroke(cx + math.cos(a) * (r + 4), cy + math.sin(a) * (r + 4),
                     cx + math.cos(a) * (r + 9), cy + math.sin(a) * (r + 9), 3)


def moon(c, cx, cy, r):
    c.disc(cx, cy, r)
    c.disc(cx + r * 0.55, cy - r * 0.35, r * 0.85, False)


def star(c, x, y, r):
    c.stroke(x - r, y, x + r, y, 2)
    c.stroke(x, y - r, x, y + r, 2)


def drops(c, y0, y1):
    for x in (15, 24, 33):
        c.stroke(x + 3, y0, x - 1, y1, 3)


def flake(c, x, y, r):
    for i in range(3):
        a = i * math.pi / 3 + math.pi / 2
        c.stroke(x - math.cos(a) * r, y - math.sin(a) * r, x + math.cos(a) * r, y + math.sin(a) * r, 2)


def icon(name):
    c = Canvas(ICON, ICON)
    if name == "sun":
        sun(c, 24, 24, 10)
    elif name == "night":
        moon(c, 22, 26, 16)
        star(c, 38, 9, 3)
        star(c, 42, 22, 2)
    elif name == "cloud":
        cloud(c, 34, 16, 0.55)
        cloud(c, 22, 29, 1.05)
    elif name == "fog":
        for y in (14, 24, 34):
            c.stroke(6, y, 42, y, 4)
        c.stroke(12, 42, 36, 42, 4)
    elif name == "rain":
        cloud(c, 24, 18, 1.0)
        drops(c, 33, 44)
    elif name == "night_rain":
        moon(c, 32, 10, 9)
        cloud(c, 22, 20, 0.95)
        drops(c, 34, 45)
    elif name == "storm":
        cloud(c, 24, 16, 1.0)
        c.polygon([(26, 26), (15, 38), (23, 38), (18, 47), (33, 33), (25, 33), (30, 26)])
    elif name == "snow":
        cloud(c, 24, 16, 1.0)
        for x, y in ((14, 36), (24, 42), (34, 36)):
            flake(c, x, y, 5)
    return c


ICONS = ["sun", "night", "cloud", "fog", "rain", "night_rain", "storm", "snow"]

# ---------------------------------------------------------------- digits

# Segments of a digit: a top, b top right, c bottom right, d bottom, e bottom left, f top left,
# g middle.
SEGMENTS = {
    "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd",
    "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg", "-": "g", "C": "afed", "F": "afeg",
}


def glyph(ch, w, h, t):
    if ch == "-":
        w = max(6, w * 2 // 3)
    if ch == "°":
        r = t + 1.5
        c = Canvas(int(2 * r) + 1, h)
        c.ring(r, r, r, t * 0.8)
        return c
    if ch == "%":
        c = Canvas(w, h)
        r = w / 4.2
        c.ring(r + 0.5, r + 1, r, t * 0.8)
        c.ring(w - r - 0.5, h - r - 1, r, t * 0.8)
        c.stroke(w - 2, 2, 1.5, h - 3, t * 0.9)
        return c
    c = Canvas(w, h)
    m = t / 2
    x0, x1, y0, ym, y1 = m, w - m, m, h / 2, h - m
    ends = {"a": (x0, y0, x1, y0), "b": (x1, y0, x1, ym), "c": (x1, ym, x1, y1),
            "d": (x0, y1, x1, y1), "e": (x0, ym, x0, y1), "f": (x0, y0, x0, ym),
            "g": (x0, ym, x1, ym)}
    for s in SEGMENTS[ch]:
        c.stroke(*ends[s], t)
    return c


FONTS = {
    # name: (width, height, stroke, characters)
    "Big": (17, 28, 4, "0123456789-°"),
    "Mid": (11, 18, 3, "0123456789-°%CF"),
}

# ---------------------------------------------------------------- output


def packed(c):
    out = []
    for row in c.px:
        for i in range(0, c.w, 8):
            b = 0
            for j in range(8):
                if i + j < c.w and row[i + j]:
                    b |= 0x80 >> j
            out.append(b)
    return out


def show(title, c):
    print(title)
    for row in c.px:
        print("".join("#" if p else "." for p in row))
    print()


def c_array(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    return "const uint8_t %s[] ART_FLASH = {\n%s\n};\n" % (name, "\n".join(lines))


def ident(ch):
    return {"-": "minus", "°": "deg", "%": "pct"}.get(ch, ch)


def main():
    showing = "--show" in sys.argv
    parts = [
        "// MeteoWifi - pictures for the 128x64 screen. Written by tools/make_art.py: do not edit.",
        "// One bit for each pixel, rows from the top, the first pixel in the high bit.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "#if defined(ARDUINO)",
        "#include <pgmspace.h>",
        "#define ART_FLASH PROGMEM",
        "#define ART_READ(p) pgm_read_byte(p)",
        "#else",
        "#define ART_FLASH",
        "#define ART_READ(p) (*(p))",
        "#endif",
        "",
        "namespace meteowifi {",
        "",
        "struct Picture {",
        "  uint8_t w, h;",
        "  const uint8_t* bits;",
        "};",
        "",
    ]
    for name in ICONS:
        c = icon(name)
        if showing:
            show("icon " + name, c)
        parts.append(c_array("kIcon_" + name, packed(c)))
    parts.append("enum class Sky : uint8_t {")
    parts.append("  " + ", ".join("%s" % "".join(w.capitalize() for w in n.split("_")) for n in ICONS) + ", Count")
    parts.append("};")
    parts.append("")
    parts.append("const Picture kIcons[] = {")
    for name in ICONS:
        parts.append("    {%d, %d, kIcon_%s}," % (ICON, ICON, name))
    parts.append("};")
    parts.append("")
    for font, (w, h, t, chars) in FONTS.items():
        for ch in chars:
            c = glyph(ch, w, h, t)
            if showing and font == "Big":
                show("%s %s" % (font, ch), c)
            parts.append(c_array("k%s_%s" % (font, ident(ch)), packed(c)))
        parts.append("// Characters of the font %s, in this order: %s" % (font, chars.replace("°", "o")))
        parts.append("const char k%sChars[] = \"%s\";" % (font, chars.replace("°", "o")))
        parts.append("const Picture k%s[] = {" % font)
        for ch in chars:
            c = glyph(ch, w, h, t)
            parts.append("    {%d, %d, k%s_%s}," % (c.w, c.h, font, ident(ch)))
        parts.append("};")
        parts.append("")
    drop = Canvas(10, 15)
    drop.polygon([(5, 0), (0.5, 9), (9.5, 9)])
    drop.disc(5, 9.8, 4.6)
    drop.disc(5, 10, 2.2, False)
    if showing:
        show("drop", drop)
    parts.append(c_array("kDropBits", packed(drop)))
    parts.append("const Picture kDrop = {%d, %d, kDropBits};" % (drop.w, drop.h))
    parts.append("")
    parts.append("}  // namespace meteowifi")
    with open(TARGET, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(parts) + "\n")
    print("written", os.path.normpath(TARGET))


if __name__ == "__main__":
    main()
