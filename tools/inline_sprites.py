#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes dist/index.html with the generated pixel art and music engine inlined.

    python3 tools/inline_sprites.py <dist-dir>

The page loads sprites.js with a script tag so that opening
frontend/web/index.html straight from a checkout still works. For a published
build the art is folded into the page instead: dist/ stays two files, and the
first paint does not wait on a second request.
"""

import pathlib
import sys

# The generated files the published page carries inside itself.
INLINE = ('sprites.js', 'music.js')


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__.strip())
    root = pathlib.Path(__file__).resolve().parent.parent
    dist = pathlib.Path(argv[1])
    dist.mkdir(parents=True, exist_ok=True)

    page = (root / 'frontend' / 'web' / 'index.html').read_text(encoding='utf-8')
    for name in INLINE:
        tag = '<script src="%s"></script>' % name
        if page.count(tag) != 1:
            raise SystemExit('the %s script tag moved; fix tools/inline_sprites.py' % name)
        body = (root / 'frontend' / 'web' / name).read_text(encoding='utf-8')
        page = page.replace(tag, '<script>\n' + body + '</script>')

    out = dist / 'index.html'
    out.write_text(page, encoding='utf-8')
    print('%s (%.1f KB)' % (out, out.stat().st_size / 1024))


if __name__ == '__main__':
    main(sys.argv)
