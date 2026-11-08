# Build-tool copies

**emdawnwebgpu_pkg-v20260423.175430.zip**: Emscripten's WebGPU port (Dawn's `webgpu.h` for the web), the
version Emscripten 6.0.13 asks for (`--use-port=emdawnwebgpu`), byte for byte (its SHA-512 is the one in
Emscripten's `tools/ports/emdawnwebgpu.py`). From
https://github.com/google/dawn/releases/download/v20260423.175430/emdawnwebgpu_pkg-v20260423.175430.zip.
Licence: some files BSD 3-Clause (the Dawn authors), the others Emscripten's (MIT or University of
Illinois/NCSA), as its own LICENSE files inside say.

Why here: on 2026-11-08 GitHub's release server timed out (504) handing it to GitHub's build machines for
hours, and every web build failed. `tools/build_site.sh` puts this copy in Emscripten's cache when the cache
lacks it, so a build never needs that download. A newer Emscripten asks for its own version and downloads it
as before.
