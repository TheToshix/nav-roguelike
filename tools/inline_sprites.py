#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes dist/index.html with the stylesheet, the page script, the generated
pixel art and the music engine folded in.

    python3 tools/inline_sprites.py <dist-dir>

In a checkout the page loads all of these with ordinary link and script tags, so
that opening frontend/web/index.html straight from the source tree still works
and so that the CSS and JS are files a linter and an editor can deal with. For a
published build they are folded into the page instead: dist/ stays two files
(the page and the WebAssembly module), and the first paint does not wait on four
more requests.

Folding is exact — the output is byte for byte the page that the source tree
would produce if everything had been written inline — so splitting a file out
here can never change what ships.
"""

import pathlib
import sys

# Folded in, in the place their tag stands. Order does not matter: each entry
# replaces its own tag.
INLINE_SCRIPTS = ('app.js', 'sprites.js', 'music.js')
INLINE_STYLES = ('app.css',)


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__.strip())
    root = pathlib.Path(__file__).resolve().parent.parent
    dist = pathlib.Path(argv[1])
    dist.mkdir(parents=True, exist_ok=True)

    web = root / 'frontend' / 'web'
    page = (web / 'index.html').read_text(encoding='utf-8')

    def fold(name, tag, open_tag, close_tag):
        nonlocal page
        if page.count(tag) != 1:
            raise SystemExit('the %s tag moved; fix tools/inline_sprites.py' % name)
        body = (web / name).read_text(encoding='utf-8')
        page = page.replace(tag, open_tag + '\n' + body + close_tag)

    for name in INLINE_SCRIPTS:
        fold(name, '<script src="%s"></script>' % name, '<script>', '</script>')
    for name in INLINE_STYLES:
        fold(name, '<link rel="stylesheet" href="%s">' % name, '<style>', '</style>')

    out = dist / 'index.html'
    out.write_text(page, encoding='utf-8')
    print('%s (%.1f KB)' % (out, out.stat().st_size / 1024))


if __name__ == '__main__':
    main(sys.argv)
