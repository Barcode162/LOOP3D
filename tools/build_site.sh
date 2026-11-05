#!/usr/bin/env bash
# Builds the web player and editor and puts the site together in site/:
#   index.html (player), editor.html + editor/ (the editor), player.js, player.wasm,
#   scenes/<name>.loop (+ <name>.png trailer stills) and scenes/index.json from projects/*/
# Needs Emscripten (emcmake). Trailer stills come from `loop render`.
# PLAYER_PREBUILT=<dir> skips the build and uses that folder's player.js/.wasm.
set -euo pipefail
cd "$(dirname "$0")/.."
rm -rf site && mkdir -p site
if [ -n "${PLAYER_PREBUILT:-}" ]; then
  cp "$PLAYER_PREBUILT/player.js" "$PLAYER_PREBUILT/player.wasm" site/
else
  emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build-web -j4
  cp build-web/player.js build-web/player.wasm site/
fi
cp web/index.html site/
cp web/editor/editor.html site/editor.html
mkdir -p site/editor && cp web/editor/*.js web/editor/*.css site/editor/
# stamp the build (benchmarks record which build they ran) and the tracker's link
stamp="$(git rev-parse --short HEAD 2>/dev/null || echo dev) $(date -u +%F)"
sed -i "s|__BUILD__|$stamp|" site/index.html site/editor.html
[ -f web/tracker_url.txt ] && sed -i "s|__TRACKER__|$(tr -d '\n' < web/tracker_url.txt)|" site/index.html
# every project's scenes, flat, plus scenes/index.json
python3 tools/build_projects.py site
echo "site/ is ready ($(du -sh site | cut -f1))"
