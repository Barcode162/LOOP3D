#!/usr/bin/env python3
"""Gets the recording page ready: the player as a private claude.ai page
that saves every visit's timings where Claude reads them (docs/TUNING.md,
section 5).

    tools/recording_page.py        (after tools/build_site.sh)

Writes build/recording/index.html (the player's page, named for the
recording page), build/recording/models/ (claude.ai serves no .glb files, so
each model goes as text: <name>.glb.json = {"glb": "<base64>"}, which the
page's loop-models tag tells the player to read) and
build/recording/files.json: the files to publish beside it, in uploads that
each fit one publish (at most 255 files and 64 MB):
    [{"published/path": {"from": "...", "contentType": "..."}}, ...]
Claude publishes the page with the Artifact tool (capabilities {"db": {}}),
the first upload's files with it and each later one to the same url. The
page keeps one core (a page inside claude.ai can't share memory between
cores), has no editor, and only the live recorder: no benchmarks.
"""
import base64
import datetime
import hashlib
import subprocess
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SITE, OUT = ROOT / "site", ROOT / "build" / "recording"
TYPES = {".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json", ".png": "image/png",
         ".loop": "text/plain"}
MAX_FILES, MAX_BYTES = 240, 60 * 1024 * 1024


def main():
    page = (SITE / "index.html").read_text()
    if "__BUILD__" in page or "player.js" not in page:
        raise SystemExit("build the site first: tools/build_site.sh")
    # claude.ai wraps a page in its own document (doctype, head with the charset
    # and viewport, body), so the page is what goes inside: the title first
    for tag in ("<!doctype html>\n", '<html lang="en">\n', "<head>\n", '<meta charset="utf-8">\n', "</head>\n", "<body>\n", "</body>\n", "</html>\n"):
        if tag not in page:
            raise SystemExit(f"the player's page changed: {tag.strip()} not found")
        page = page.replace(tag, "", 1)
    page = re.sub(r'<meta name="viewport"[^>]*>\n', "", page, count=1)
    page = page.replace("<title>Loop3D Player</title>", "<title>Loop3D Recording</title>", 1)
    if not page.startswith("<title>Loop3D Recording</title>"):
        raise SystemExit("the player's page changed: its title is not first")
    # the page may reach the screen's sides (its bars keep clear of the notch with env(safe-area-inset-*)),
    # and it is always dark (the wrapper starts pages light)
    page = page.replace("<title>Loop3D Recording</title>\n", '<title>Loop3D Recording</title>\n<meta name="claude:viewport-fit-sides" content="cover">\n', 1)
    page = page.replace("<style>\n  :root {", "<style>\n  :root { color-scheme: dark;", 1)
    page = page.replace('<meta name="loop-build"', '<meta name="loop-models" content="json">\n<meta name="loop-build"', 1)
    # the build it is, named when published (the site is built before its commit,
    # so its own stamp named the commit before: every recording looked a step old)
    head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip() or "dev"
    dirty = subprocess.run(["git", "status", "--porcelain", "--", "engine", "web", "tools"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    stamp = f"{head}{' +changes' if dirty else ''} {datetime.date.today().isoformat()}"
    page = re.sub(r'<meta name="loop-build" content="[^"]*">', f'<meta name="loop-build" content="{stamp}">', page, count=1)
    # the engine's files named by their contents: a new version is always a new
    # file, so no copy kept by the browser or the app can stand in for it
    engine = ["loop_ui.js", "isolate.js", "player.js", "player.wasm"]
    code = hashlib.sha1(b"".join((SITE / f).read_bytes() for f in engine)).hexdigest()[:10]
    named = {f: re.sub(r"(\.[a-z]+)$", f"-{code}\\1", f) for f in engine}
    page = page.replace('<meta name="loop-build"', f'<meta name="loop-code" content="{code}">\n<meta name="loop-build"', 1)
    for f in ("isolate.js", "loop_ui.js"):
        if f'<script src="{f}"></script>' not in page:
            raise SystemExit(f"the player's page changed: its {f} tag not found")
        page = page.replace(f'<script src="{f}"></script>', f'<script src="{named[f]}"></script>', 1)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "index.html").write_text(page)

    # published path -> source on disk (relative to the repo)
    files = {named[f]: f"site/{f}" for f in engine}
    models = OUT / "models"
    models.mkdir(exist_ok=True)
    for p in sorted((SITE / "scenes").rglob("*")):
        if not p.is_file():
            continue
        f = str(p.relative_to(SITE))
        if p.suffix == ".glb":
            text = models / (p.name + ".json")
            text.write_text(json.dumps({"glb": base64.b64encode(p.read_bytes()).decode()}))
            files[f + ".json"] = str(text.relative_to(ROOT))
        else:
            files[f] = f"site/{f}"
    # the models last, so the first upload holds the player and every scene
    order = sorted(files, key=lambda f: f.startswith("scenes/models/") and f.endswith(".glb.json"))
    uploads, cur, size = [], {}, 0
    for f in order:
        n = (ROOT / files[f]).stat().st_size
        model = f.startswith("scenes/models/") and f.endswith(".glb.json")
        if cur and (len(cur) >= MAX_FILES or size + n > MAX_BYTES or (model and not any(k.endswith(".glb.json") for k in cur))):
            uploads.append(cur)
            cur, size = {}, 0
        cur[f] = {"from": files[f], "contentType": TYPES[pathlib.Path(f).suffix]}
        size += n
    if cur:
        uploads.append(cur)
    (OUT / "files.json").write_text(json.dumps(uploads, indent=1))
    total = sum((ROOT / s).stat().st_size for s in files.values())
    print(f"build/recording/: the page and {len(files)} files ({total / 1048576:.1f} MB) in {len(uploads)} uploads: "
          + ", ".join(f"{len(u)} files, {sum((ROOT / v['from']).stat().st_size for v in u.values()) / 1048576:.1f} MB" for u in uploads))


if __name__ == "__main__":
    main()
