#!/usr/bin/env bash
# Builds the web player and puts the site together in site/:
#   index.html, player.js, player.wasm, scenes/<name>.loop (+ <name>.png trailer stills)
# Needs Emscripten (emcmake). Trailer stills come from `loop render` (out/<name>.png).
set -euo pipefail
cd "$(dirname "$0")/.."
emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-web -j4
rm -rf site && mkdir -p site/scenes
cp web/index.html build-web/player.js build-web/player.wasm site/
cp examples/*.loop examples/story/*.loop site/scenes/
for f in examples/*.loop; do
  n=$(basename "$f" .loop)
  [ -f "examples/stills/$n.png" ] && cp "examples/stills/$n.png" "site/scenes/$n.png"
done
echo "site/ is ready ($(du -sh site | cut -f1))"
