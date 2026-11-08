#!/usr/bin/env bash
# Builds the web player and editor and puts the site together in site/:
#   index.html (player), editor.html + editor/ (the editor), player.js, player.wasm (one core),
#   player_mt.js, player_mt.wasm (every core, when the page is isolated: isolate.js),
#   scenes/<name>.loop (+ <name>.png trailer stills) and scenes/index.json from projects/*/
# Needs Emscripten (emcmake). Trailer stills come from `loop render`.
# PLAYER_PREBUILT=<dir> skips the build and uses that folder's player.js/.wasm.
set -euo pipefail
cd "$(dirname "$0")/.."
rm -rf site && mkdir -p site
if [ -n "${PLAYER_PREBUILT:-}" ]; then
  cp "$PLAYER_PREBUILT/player.js" "$PLAYER_PREBUILT/player.wasm" site/
  if [ -f "$PLAYER_PREBUILT/player_mt.js" ]; then cp "$PLAYER_PREBUILT/player_mt.js" "$PLAYER_PREBUILT/player_mt.wasm" site/; fi
else
  # Emscripten's WebGPU port from our own copy (tools/ports/README.md) when its cache lacks it
  ver=v20260423.175430
  url=https://github.com/google/dawn/releases/download/$ver/emdawnwebgpu_pkg-$ver.zip
  ports="$(em-config CACHE)/ports"
  if grep -q "_VERSION = '$ver'" "$(em-config EMSCRIPTEN_ROOT)/tools/ports/emdawnwebgpu.py" 2>/dev/null &&
     [ "$(cat "$ports/emdawnwebgpu/.emscripten_url" 2>/dev/null)" != "$url" ]; then
    mkdir -p "$ports" && rm -rf "$ports/emdawnwebgpu"
    python3 -c 'import shutil,sys; shutil.unpack_archive(sys.argv[1], sys.argv[2])' "tools/ports/emdawnwebgpu_pkg-$ver.zip" "$ports/emdawnwebgpu"
    cp "tools/ports/emdawnwebgpu_pkg-$ver.zip" "$ports/emdawnwebgpu.${ver#*.}.zip"
    echo "$url" > "$ports/emdawnwebgpu/.emscripten_url"
    echo "Emscripten's WebGPU port: from tools/ports/"
  fi
  # build.opt from the projects' project.json (the first that says), else O3
  opt=$(python3 -c 'import json,glob;print(next((json.load(open(f)).get("build",{}).get("opt") for f in sorted(glob.glob("projects/*/project.json")) if json.load(open(f)).get("build",{}).get("opt")),"O3"))')
  emcmake cmake -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release "-DLOOP_OPT=-$opt" >/dev/null
  cmake --build build-web -j4
  cp build-web/player.js build-web/player.wasm build-web/player_mt.js build-web/player_mt.wasm site/
fi
cp web/index.html web/loop_ui.js web/isolate.js site/
cp web/editor/editor.html site/editor.html
mkdir -p site/editor && cp web/editor/*.js web/editor/*.css site/editor/
# stamp the build (benchmarks record which build they ran) and the tracker's link
stamp="$(git rev-parse --short HEAD 2>/dev/null || echo dev) $(date -u +%F)"
# build.stamp: false in a project.json leaves the pages unstamped
nostamp=$(python3 -c 'import json,glob;print(any(json.load(open(f)).get("build",{}).get("stamp") is False for f in glob.glob("projects/*/project.json")))')
[ "$nostamp" = "True" ] && stamp=""
sed -i "s|__BUILD__|$stamp|" site/index.html site/editor.html
[ -f web/tracker_url.txt ] && sed -i "s|__TRACKER__|$(tr -d '\n' < web/tracker_url.txt)|" site/index.html
# every project's scenes, flat, plus scenes/index.json
python3 tools/build_projects.py site
echo "site/ is ready ($(du -sh site | cut -f1))"
