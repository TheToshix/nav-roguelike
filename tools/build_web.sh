#!/usr/bin/env bash
# Builds the browser version into dist/.
#
# Requires the Emscripten SDK on PATH (emcmake / em++). The result is two
# files — index.html and nav.js — that can be served from any static host,
# including GitHub Pages. The wasm binary is embedded in nav.js
# (-sSINGLE_FILE=1), so there is nothing else to upload and no MIME type to
# configure.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build-web"
dist="$root/dist"

if ! command -v emcmake >/dev/null 2>&1; then
  echo "emcmake not found. Install the Emscripten SDK first:" >&2
  echo "  git clone https://github.com/emscripten-core/emsdk && cd emsdk" >&2
  echo "  ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh" >&2
  exit 1
fi

echo "==> configuring"
emcmake cmake -S "$root" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNAV_BUILD_TESTS=OFF \
  -DNAV_BUILD_TERMINAL=OFF >/dev/null

echo "==> building"
cmake --build "$build" -j"$(nproc 2>/dev/null || echo 4)"

echo "==> packaging into dist/"
mkdir -p "$dist"
cp "$root/frontend/web/index.html" "$dist/index.html"
cp "$build/nav.js" "$dist/nav.js"
touch "$dist/.nojekyll"   # GitHub Pages otherwise hides files starting with _

echo
echo "dist/ is ready ($(du -sh "$dist" | cut -f1)):"
ls -la "$dist"
echo
echo "Serve it locally with:  python3 -m http.server -d dist 8080"
