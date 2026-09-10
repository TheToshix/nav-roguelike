#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes the Android launcher icons into a generated project's res/ directory.

    python3 tools/icons/android.py mobile/android/app/src/main/res

Capacitor's template ships a placeholder icon. This replaces it with the game's
own art, at every density Android asks for, from the same sprite the web icons
come from — so the phone icon and the browser icon can never drift apart.

Three files per density, because Android has three ideas about launcher icons:

    ic_launcher.png             the plain one, used by older launchers
    ic_launcher_round.png       the same art, for launchers that want a circle
    ic_launcher_foreground.png  the top layer of an adaptive icon

An adaptive icon is two layers that the launcher masks to whatever shape it
likes and may parallax when the user scrolls. Only the middle ~66% of the
foreground is guaranteed to survive the mask, so that layer is drawn smaller,
transparent around the art, over a solid background colour supplied by the
project's ic_launcher_background — which this also writes.

Run from CI after `npx cap add android`; the res/ directory does not exist
before that.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    from PIL import Image
except ImportError:
    raise SystemExit('Pillow is required: pip install pillow')

from build import GROUND, SUBJECT, sprite_image  # noqa: E402

# Android's five buckets, and the launcher icon size each one wants.
DENSITIES = {
    'mdpi': 48,
    'hdpi': 72,
    'xhdpi': 96,
    'xxhdpi': 144,
    'xxxhdpi': 192,
}

# An adaptive icon's foreground is drawn on a canvas 1.5x the visible icon, and
# the mask can eat everything outside the middle. 0.45 keeps the drawing well
# inside the safe zone on every launcher shape.
ADAPTIVE_CANVAS = 1.5
ADAPTIVE_ART = 0.45

# The plain and round icons are the full square, art at a comfortable inset.
PLAIN_ART = 0.80


# Every icon is composed this many times larger than it will be shown, then
# reduced by exactly that factor. See render().
SUPERSAMPLE = 8


def render(size, art_fraction, ground):
    """`size` square, the sprite centred at `art_fraction` of the width.

    Composed at eight times the final size and reduced by an exact factor of
    eight, rather than drawn at the final size directly. The reason is a trap
    worth writing down: the sprite is 48 pixels across and Android wants icons
    at 48, 72, 96, 144 and 192. Scaling the drawing by a whole number — the
    thing that keeps pixel art from turning to mush — then leaves it covering
    100% of the 48-pixel icon, 67% of the 72, and 50% of the 96. The same app
    would wear a visibly different icon depending on the phone.

    Composing large makes the whole-number step small enough not to matter: the
    drawing lands within a couple of percent of `art_fraction` at every density.
    The reduction is an exact 8:1 box filter, which is a plain average of each
    8x8 block — no resampling guesswork, and no ringing along the hard edges
    that pixel art is made of.
    """
    big = size * SUPERSAMPLE
    art = sprite_image(SUBJECT)
    scale = max(1, int(big * art_fraction) // art.width)
    art = art.resize((art.width * scale, art.height * scale), Image.NEAREST)

    canvas = Image.new('RGBA', (big, big), ground)
    canvas.paste(art, ((big - art.width) // 2, (big - art.height) // 2), art)
    return canvas.resize((size, size), Image.BOX)


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__.strip())
    res = argv[1]
    if not os.path.isdir(res):
        raise SystemExit('%s does not exist — run `npx cap add android` first' % res)

    written = 0
    for density, size in DENSITIES.items():
        out_dir = os.path.join(res, 'mipmap-' + density)
        os.makedirs(out_dir, exist_ok=True)

        plain = render(size, PLAIN_ART, GROUND)
        plain.save(os.path.join(out_dir, 'ic_launcher.png'))
        plain.save(os.path.join(out_dir, 'ic_launcher_round.png'))

        foreground = render(int(size * ADAPTIVE_CANVAS), ADAPTIVE_ART, (0, 0, 0, 0))
        foreground.save(os.path.join(out_dir, 'ic_launcher_foreground.png'))
        written += 3

    # The adaptive icon's background layer: one flat colour, declared as a
    # resource rather than baked into a bitmap, which is how Android prefers it.
    values = os.path.join(res, 'values')
    os.makedirs(values, exist_ok=True)
    colour = '#%02X%02X%02X' % GROUND[:3]
    with open(os.path.join(values, 'ic_launcher_background.xml'), 'w', encoding='utf-8') as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<resources>\n'
                '    <color name="ic_launcher_background">%s</color>\n'
                '</resources>\n' % colour)

    print('%d icons written under %s (subject: %s, ground: %s)'
          % (written, res, SUBJECT, colour))


if __name__ == '__main__':
    main(sys.argv)
