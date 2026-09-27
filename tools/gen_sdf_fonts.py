# Signed-distance-field versions of the UI fonts, as BMFont .fnt + .png.
#
# Each glyph is rasterised big, turned into a distance field and shrunk into the
# atlas: the alpha channel holds 0.5 on the outline, more inside, less outside.
# Text.cpp draws them with a shader that cuts the edge at 0.5, so text stays
# sharp at any size (Geode's generated fonts are plain bitmaps that go blotchy
# when shrunk).
#
# Glyph placement copies Geode's font generator (baseline one em below the top
# of the line, ink-tight boxes), so layouts don't move.
#
# Needs Pillow, numpy and scipy. Run from the repo root after changing the
# icon charset in mod.json:  python tools/gen_sdf_fonts.py

import json
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt

EM = 64          # atlas pixels per em
SPREAD = 6       # atlas pixels of distance on each side of the outline
OVERSAMPLE = 8   # rasterise at EM * OVERSAMPLE, then average down
ATLAS_W = 1024
OUT = 'resources/fonts/sdf'

TEXT = [chr(c) for c in range(32, 127)] + ['•']
mod = json.load(open('mod.json', encoding='utf-8'))
ICONS = [chr(int(c)) for c in mod['resources']['fonts']['icons']['charset'].split(',')]

FONTS = [
    # name, ttf, characters, line height in em (what Geode's generator gives)
    ('outfit-regular', 'Outfit-Regular.ttf', TEXT, 1.26),
    ('outfit-semibold', 'Outfit-SemiBold.ttf', TEXT, 1.26),
    ('outfit-bold', 'Outfit-Bold.ttf', TEXT, 1.26),
    ('icons', 'fa-solid-900.ttf', ICONS, 100.125 / 96),
]


def glyph(font, big, ch):
    """Distance field of one glyph, plus its metrics in atlas pixels."""
    size = EM * OVERSAMPLE
    canvas = Image.new('L', (size * 3, size * 3))
    origin = (size, size * 2)  # left end of the baseline
    ImageDraw.Draw(canvas).text(origin, ch, font=big, fill=255, anchor='ls')
    ink = np.array(canvas) >= 128
    advance = round(font.getlength(ch))
    ys, xs = np.nonzero(ink)
    if len(xs) == 0:
        return None, {'xoffset': 0, 'yoffset': 0, 'xadvance': advance}

    # Ink box, grown to whole atlas pixels, then padded by the spread.
    left = (xs.min() - origin[0]) // OVERSAMPLE
    top_px = (ys.min() - origin[1]) // OVERSAMPLE          # atlas px above the baseline (negative)
    right = -(-(xs.max() + 1 - origin[0]) // OVERSAMPLE)
    bottom = -(-(ys.max() + 1 - origin[1]) // OVERSAMPLE)
    x0 = origin[0] + (left - SPREAD) * OVERSAMPLE
    y0 = origin[1] + (top_px - SPREAD) * OVERSAMPLE
    x1 = origin[0] + (right + SPREAD) * OVERSAMPLE
    y1 = origin[1] + (bottom + SPREAD) * OVERSAMPLE
    region = ink[y0:y1, x0:x1]

    # Signed distance in big pixels (positive inside), averaged down to atlas pixels.
    inside = distance_transform_edt(region) - 0.5
    outside = distance_transform_edt(~region) - 0.5
    dist = np.where(region, inside, -outside) / OVERSAMPLE
    h, w = dist.shape[0] // OVERSAMPLE, dist.shape[1] // OVERSAMPLE
    dist = dist.reshape(h, OVERSAMPLE, w, OVERSAMPLE).mean(axis=(1, 3))
    alpha = np.clip(0.5 + dist / (2 * SPREAD), 0, 1)
    return (alpha * 255 + 0.5).astype(np.uint8), {
        'xoffset': left - SPREAD,
        'yoffset': EM + top_px - SPREAD,  # from the top of the line: baseline sits one em down
        'xadvance': advance,
    }


def build(name, ttf, chars, line_em):
    path = os.path.join('resources/fonts', ttf)
    font = ImageFont.truetype(path, EM)
    big = ImageFont.truetype(path, EM * OVERSAMPLE)
    glyphs = [(ch, *glyph(font, big, ch)) for ch in chars]

    # Shelf packing, tallest first.
    order = sorted(range(len(glyphs)), key=lambda i: -(glyphs[i][1].shape[0] if glyphs[i][1] is not None else 0))
    x = y = shelf = 0
    places = {}
    for i in order:
        img = glyphs[i][1]
        if img is None:
            places[i] = (0, 0, 0, 0)
            continue
        h, w = img.shape
        if x + w > ATLAS_W:
            x, y, shelf = 0, y + shelf + 1, 0
        places[i] = (x, y, w, h)
        x += w + 1
        shelf = max(shelf, h)
    height = y + shelf
    height = -(-height // 4) * 4

    atlas = np.zeros((height, ATLAS_W), np.uint8)
    for i, (ch, img, m) in enumerate(glyphs):
        px, py, w, h = places[i]
        if img is not None:
            atlas[py:py + h, px:px + w] = img
    rgba = np.zeros((height, ATLAS_W, 4), np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = atlas
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(rgba, 'RGBA').save(os.path.join(OUT, f'{name}-sdf.png'), optimize=True)

    lines = [
        f'info face="{ttf}" size={EM} bold=0 italic=0 charset="" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=1,1',
        f'common lineHeight={round(line_em * EM)} base={EM} scaleW={ATLAS_W} scaleH={height} pages=1 packed=0',
        f'page id=0 file="{name}-sdf.png"',
        f'chars count={len(glyphs)}',
    ]
    for i, (ch, img, m) in enumerate(glyphs):
        px, py, w, h = places[i]
        lines.append(f'char id={ord(ch)} x={px} y={py} width={w} height={h} '
                     f'xoffset={m["xoffset"]} yoffset={m["yoffset"]} xadvance={m["xadvance"]} page=0 chnl=0')
    lines.append('kernings count=0')
    with open(os.path.join(OUT, f'{name}-sdf.fnt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'{name}: {len(glyphs)} glyphs, {ATLAS_W}x{height}')


for args in FONTS:
    build(*args)
print(f'spread={SPREAD} em={EM}: keep SDF_SPREAD in src/ui/core/Text.cpp in sync')
