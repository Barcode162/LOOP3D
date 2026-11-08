#!/usr/bin/env python3
"""Gets the recording page ready: the player as a private claude.ai page
that saves every visit's timings where Claude reads them (docs/TUNING.md,
section 5).

    tools/recording_page.py        (after tools/build_site.sh)

Writes build/recording/index.html (the player's page, named for the
recording page) and build/recording/files.json: the files to publish beside
it, in uploads that each fit one publish (at most 255 files and 64 MB):
    [{"published/path": {"from": "site/...", "contentType": "..."}}, ...]
Claude publishes the page with the Artifact tool (capabilities {"db": {}}),
the first upload's files with it and each later one to the same url. The
page keeps one core (a page inside claude.ai can't share memory between
cores), has no editor, and only the live recorder: no benchmarks.
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SITE, OUT = ROOT / "site", ROOT / "build" / "recording"
TYPES = {".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json", ".png": "image/png",
         ".glb": "model/gltf-binary", ".loop": "text/plain"}
MAX_FILES, MAX_BYTES = 240, 60 * 1024 * 1024


def main():
    page = (SITE / "index.html").read_text()
    if "__BUILD__" in page or "player.js" not in page:
        raise SystemExit("build the site first: tools/build_site.sh")
    page = page.replace("<title>Loop3D Player</title>", "<title>Loop3D Recording</title>", 1)
    # the page may reach the screen's sides (the notch and the round corners stay clear through env(safe-area-inset-*))
    page = page.replace('<meta name="loop-build"', '<meta name="claude:viewport-fit-sides" content="cover">\n<meta name="loop-build"', 1)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "index.html").write_text(page)

    files = ["loop_ui.js", "isolate.js", "player.js", "player.wasm"]
    files += sorted(str(p.relative_to(SITE)) for p in (SITE / "scenes").rglob("*") if p.is_file())
    # the models last, so the first upload holds the player and every scene
    files.sort(key=lambda f: f.startswith("scenes/models/"))
    uploads, cur, size = [], {}, 0
    for f in files:
        n = (SITE / f).stat().st_size
        if cur and (len(cur) >= MAX_FILES or size + n > MAX_BYTES or (f.startswith("scenes/models/") and not any(k.startswith("scenes/models/") for k in cur))):
            uploads.append(cur)
            cur, size = {}, 0
        cur[f] = {"from": f"site/{f}", "contentType": TYPES.get(pathlib.Path(f).suffix, "application/octet-stream")}
        size += n
    if cur:
        uploads.append(cur)
    (OUT / "files.json").write_text(json.dumps(uploads, indent=1))
    total = sum((SITE / f).stat().st_size for f in files)
    print(f"build/recording/: the page and {len(files)} files ({total / 1048576:.1f} MB) in {len(uploads)} uploads: "
          + ", ".join(str(len(u)) for u in uploads))


if __name__ == "__main__":
    main()
