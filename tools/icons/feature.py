#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Builds the Google Play feature graphic: 1024x500, from the game's own parts.

    python3 tools/icons/feature.py [out.png]

Play shows this banner at the top of the store listing, and on some surfaces it
draws a play button over the middle of it. So nothing important goes in the
centre: the wordmark rides the upper half and the four masters of the belts
stand across the lower one, leaving the middle as ground on purpose.

Everything on it comes from the repository. The wordmark is the same block-letter
NAV that the title screen prints — read out of frontend/web/index.html rather
than retyped, so it cannot drift from what a player sees. The creatures are the
four belt masters, in the order they are met. The colours are the game's.

No text is drawn beyond the wordmark. Play overlays the app's real name and
short description near this image, and a banner that repeats them just reads as
noise — worse, the store shows the graphic at several sizes and small type turns
to mud.

Requires Pillow.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

try:
    from PIL import Image
except ImportError:
    raise SystemExit('Pillow is required: pip install pillow')

from build import GROUND, sprite_image  # noqa: E402

WIDTH, HEIGHT = 1024, 500

# The master of each belt, in the order the descent meets them: Погост,
# Чернотопь, Кощеево царство, Пекло.
#
# All eight guardians were tried first and looked worse. Eight figures across
# 1024 pixels come out about a fifth of the frame tall each — too small to read
# as anything but a row of specks, with a dead band above them. Four of them get
# twice the size and tell the same story: four belts, four things waiting at the
# bottom of one.
GUARDIANS = ['viy', 'babayaga', 'koschei', 'gorynych']

GOLD = (200, 164, 92, 255)     # --gold, the title screen's letter colour

# Composed at this multiple and reduced, for the reason spelt out in android.py:
# whole-number sprite scaling is what keeps pixel art sharp, and composing large
# is what stops that whole number from throwing the layout off.
SUPERSAMPLE = 2


def wordmark_rows():
    """The block-letter NAV, lifted out of the page rather than retyped."""
    page = os.path.join(ROOT, 'frontend', 'web', 'index.html')
    with open(page, encoding='utf-8') as f:
        html = f.read()
    match = re.search(r'<pre class="wordmark"[^>]*>(.*?)</pre>', html, re.S)
    if not match:
        raise SystemExit('the wordmark moved in index.html; fix tools/icons/feature.py')
    rows = [line.rstrip('\n') for line in match.group(1).split('\n')]
    return [r for r in rows if r.strip()]


def draw_wordmark(canvas, rows, origin, cell):
    """Paints the block letters as filled squares, one per '█'."""
    from PIL import ImageDraw
    d = ImageDraw.Draw(canvas)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != '█':
                continue
            left = origin[0] + x * cell
            top = origin[1] + y * cell
            d.rectangle([left, top, left + cell - 1, top + cell - 1], fill=GOLD)


def build():
    big_w, big_h = WIDTH * SUPERSAMPLE, HEIGHT * SUPERSAMPLE
    canvas = Image.new('RGBA', (big_w, big_h), GROUND)

    # --- the masters, across the lower half -------------------------------
    #
    # Ordered left to right as the descent meets them, so the row reads as the
    # journey rather than as a line-up.
    sprites = [sprite_image(k) for k in GUARDIANS]
    gap = int(big_w * 0.020)

    # Scale is driven by the row's total *width*, not by one sprite's height.
    # Driving it from height overflowed the frame and cropped the creatures at
    # both ends — the guardians are not all the same width, and eight of them
    # add up faster than they look like they will.
    usable = int(big_w * 0.84) - gap * (len(sprites) - 1)
    scale = max(1, usable // sum(s.width for s in sprites))
    drawn = [s.resize((s.width * scale, s.height * scale), Image.NEAREST) for s in sprites]
    total = sum(s.width for s in drawn) + gap * (len(drawn) - 1)

    # Feet on a common line with room beneath, so the row reads as standing
    # rather than as cropped.
    baseline = int(big_h * 0.94)
    x = (big_w - total) // 2
    for s in drawn:
        canvas.paste(s, (x, baseline - s.height), s)
        x += s.width + gap

    # --- the wordmark, centred in the space above them --------------------
    #
    # Sized and placed against whatever height the row above actually took, so
    # the two halves stay balanced if the sprites or the frame ever change.
    rows = wordmark_rows()
    width_cells = max(len(r) for r in rows)
    cell = int(big_w * 0.40) // width_cells
    mark_w = width_cells * cell
    mark_h = len(rows) * cell
    ceiling = baseline - max(s.height for s in drawn)
    draw_wordmark(canvas, rows,
                  ((big_w - mark_w) // 2, (ceiling - mark_h) // 2),
                  cell)

    return canvas.resize((WIDTH, HEIGHT), Image.BOX).convert('RGB')


def main(argv):
    out = argv[1] if len(argv) > 1 else os.path.join(ROOT, 'docs', 'media', 'feature-graphic.png')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img = build()
    # Play rejects transparency here, which is why this is saved as RGB.
    img.save(out)
    print('%s %dx%d  (%.1f KB)'
           % (os.path.relpath(out, ROOT), img.width, img.height,
              os.path.getsize(out) / 1024))


if __name__ == '__main__':
    main(sys.argv)
