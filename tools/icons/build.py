#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Builds every launcher and store icon the game needs, from its own pixel art.

    python3 tools/icons/build.py

Nothing here is drawn by hand. The icon is Вий — the guardian at the bottom of
the first belt, and the only sprite in the game that is unmistakably a face —
taken straight out of tools/sprites/pixels.py and scaled with nearest-neighbour
onto the game's own background. The art stays authored in one place: edit the
pixels, rerun this, and the phone icon changes with the game.

Nearest-neighbour and integer scale factors are the whole point. A 16x16 drawing
smoothed up to 512x512 turns to mush; multiplied by 32 it stays the drawing it
was, which is what a player recognises on a home screen full of gradients.

Outputs, all under frontend/web/icons/ unless noted:

    icon-512.png        Play Store listing, and the PWA's largest icon
    icon-192.png        PWA install, Android home screen
    icon-maskable.png   same art inside Android's safe zone (see below)
    icon-64.png         browser tab and small surfaces
    ../favicon.png      what index.html points at

Requires Pillow (`pip install pillow`), the same dependency the sprite contact
sheet already uses.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'sprites'))

try:
    from PIL import Image
except ImportError:
    raise SystemExit('Pillow is required: pip install pillow')

from pixels import SPRITES  # noqa: E402

# The face on the icon. Змей Горыныч, the guardian on the last floor: three
# heads give a silhouette nothing else on a home screen has, and the warm orange
# is the only thing in the palette that carries at 48 pixels on a dark ground.
#
# The alternatives were looked at side by side rather than argued about. The six
# heroes read as "an armoured figure" and as each other. Вий, whose face is the
# game's best drawing at full size, collapses at icon size into a yellow box
# with a stripe — closer to a bus than to a god. Кощей reads well and is the
# runner-up.
#
# Guardians are drawn at 48x48 rather than the ordinary 16x16, which is three
# times the detail to scale up from.
SUBJECT = 'gorynych'

# The page's own background, so the icon and the game agree. Taken from
# --bg in frontend/web/app.css.
GROUND = (20, 17, 15, 255)

OUT_DIR = os.path.join(ROOT, 'frontend', 'web', 'icons')


def sprite_image(key):
    """One sprite as an RGBA image, transparent where the art is.

    Size comes from the sprite itself: ordinary creatures are 16x16, guardians
    48x48. Reading it from the row data rather than from a constant is what
    lets the subject be swapped for any other sprite without touching this.
    """
    spr = SPRITES[key]
    palette = spr['palette']
    size = len(spr['rows'])
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    px = img.load()
    for y, row in enumerate(spr['rows']):
        for x, ch in enumerate(row):
            if ch == '.':
                continue
            hex_colour = palette[ch].lstrip('#')
            px[x, y] = (int(hex_colour[0:2], 16), int(hex_colour[2:4], 16),
                        int(hex_colour[4:6], 16), 255)
    return img


def compose(size, art_fraction):
    """The icon at `size`, with the drawing occupying `art_fraction` of it.

    `art_fraction` is what separates the two kinds of icon Android wants. A
    normal icon fills its square. A *maskable* one may be cropped to a circle,
    a squircle or a rounded square depending on the launcher, and only the
    middle ~80% of it is guaranteed to survive — so the art is shrunk into that
    safe zone and the ground is left to be eaten.
    """
    art = sprite_image(SUBJECT)
    scale = max(1, int(size * art_fraction) // art.width)   # integer, or it blurs
    art = art.resize((art.width * scale, art.height * scale), Image.NEAREST)

    icon = Image.new('RGBA', (size, size), GROUND)
    icon.paste(art, ((size - art.width) // 2, (size - art.height) // 2), art)
    return icon


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    made = []

    for size in (512, 192, 64):
        path = os.path.join(OUT_DIR, 'icon-%d.png' % size)
        compose(size, 0.82).save(path)
        made.append(path)

    maskable = os.path.join(OUT_DIR, 'icon-maskable.png')
    compose(512, 0.60).save(maskable)
    made.append(maskable)

    favicon = os.path.join(ROOT, 'frontend', 'web', 'favicon.png')
    compose(64, 0.82).save(favicon)
    made.append(favicon)

    for path in made:
        with Image.open(path) as im:
            print('%-46s %dx%d' % (os.path.relpath(path, ROOT), im.width, im.height))


if __name__ == '__main__':
    main()
