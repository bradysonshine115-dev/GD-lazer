"""Draws the extra cursor states (text, hand, resize...) in the style of osu!'s
menu-cursor: a white 21 px line inside a 50% black halo 12 px wide on each side,
on a 512 x 512 canvas, plus an additive layer (white at 50% over the whole
shape) for the pink press glow. osu! only ships the arrow.

    python desktop-cursor/make_cursors.py   -> desktop-cursor/res/cursor-*.png
"""

import math
import os

from PIL import Image, ImageChops, ImageDraw

SS = 4  # supersampling
SIZE = 512
LINE = 21  # the white line
HALO = LINE + 2 * 12  # the black halo around it
OUT = os.path.join(os.path.dirname(__file__), "res")


class Shape:
    def __init__(self):
        self.halo = Image.new("L", (SIZE * SS, SIZE * SS), 0)
        self.white = Image.new("L", (SIZE * SS, SIZE * SS), 0)

    def _stroke(self, mask, points, width, closed):
        draw = ImageDraw.Draw(mask)
        pts = [(x * SS, y * SS) for x, y in points]
        if closed:
            pts = pts + [pts[0]]
        draw.line(pts, fill=255, width=int(width * SS), joint="curve")
        r = width * SS / 2
        for x, y in pts:  # round caps and joins
            draw.ellipse((x - r, y - r, x + r, y + r), fill=255)

    def line(self, points, closed=False):
        self._stroke(self.halo, points, HALO, closed)
        self._stroke(self.white, points, LINE, closed)

    def fill(self, points):
        """A closed outline with the halo's black inside it (like the arrow)."""
        ImageDraw.Draw(self.halo).polygon([(x * SS, y * SS) for x, y in points], fill=255)
        self.line(points, closed=True)

    def arc(self, cx, cy, r, start, end, steps=96):
        pts = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
               for a in (start + (end - start) * i / steps for i in range(steps + 1))]
        self.line(pts)

    def save(self, name):
        halo = self.halo.resize((SIZE, SIZE), Image.LANCZOS)
        white = self.white.resize((SIZE, SIZE), Image.LANCZOS)
        w = white.point(lambda v: v / 255)
        base = Image.new("RGBA", (SIZE, SIZE))
        px = base.load()
        hp, wp = halo.load(), white.load()
        for y in range(SIZE):
            for x in range(SIZE):
                wv, hv = wp[x, y] / 255, hp[x, y] / 255
                a = wv + (1 - wv) * hv * 128 / 255  # white over 50% black
                if a <= 0:
                    continue
                c = round(255 * wv / a)
                px[x, y] = (c, c, c, round(a * 255))
        base.save(os.path.join(OUT, f"cursor-{name}.png"))
        additive = Image.merge("RGBA", (*[Image.new("L", (SIZE, SIZE), 255)] * 3, halo.point(lambda v: v * 128 // 255)))
        additive.save(os.path.join(OUT, f"cursor-{name}-additive.png"))


def rotated(points, degrees, cx=256, cy=256):
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    return [(cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c) for x, y in points]


def double_arrow(shape, degrees, half=150, head=56):
    """A two-headed arrow through the middle, vertical before turning."""
    shape.line(rotated([(256, 256 - half), (256, 256 + half)], degrees))
    for sign in (-1, 1):
        tip = 256 + sign * half
        shape.line(rotated([(256 - head, tip - sign * head), (256, tip), (256 + head, tip - sign * head)], degrees))


def main():
    os.makedirs(OUT, exist_ok=True)

    # Text: an I-beam, hotspot in the middle.
    s = Shape()
    s.line([(256, 116), (256, 396)])
    s.line([(206, 116), (306, 116)])
    s.line([(206, 396), (306, 396)])
    s.save("ibeam")

    # Link: a pointing hand, hotspot at the fingertip (200, 44).
    s = Shape()
    hand = [
        (172, 80), (180, 56), (200, 44), (220, 56), (228, 80),  # index finger
        (228, 210), (236, 196), (258, 188), (280, 198), (284, 214),  # middle
        (290, 214), (308, 206), (328, 214), (334, 232),  # ring
        (340, 238), (358, 234), (376, 246), (380, 266),  # little
        (380, 350), (366, 398), (330, 428), (230, 428), (186, 406),  # palm
        (150, 360), (110, 310), (96, 282), (104, 262), (128, 262), (172, 304),  # thumb
    ]
    s.fill(hand)
    s.save("hand")

    for name, degrees in (("size-ns", 0), ("size-we", 90), ("size-nwse", -45), ("size-nesw", 45)):
        s = Shape()
        double_arrow(s, degrees)
        s.save(name)

    s = Shape()
    double_arrow(s, 0)
    double_arrow(s, 90)
    s.save("size-all")

    # Crosshair: a plus open in the middle, with a dot there.
    s = Shape()
    for degrees in (0, 90, 180, 270):
        s.line(rotated([(256, 196), (256, 96)], degrees))
    s.line([(256, 256)])
    s.save("cross")

    # Not allowed: a ring with a slash.
    s = Shape()
    s.arc(256, 256, 120, 0, 360)
    s.line(rotated([(256, 136), (256, 376)], 45))
    s.save("no")

    # Busy: an open ring, spun by the app.
    s = Shape()
    s.arc(256, 256, 110, -90, 180)
    s.save("wait")


if __name__ == "__main__":
    main()
