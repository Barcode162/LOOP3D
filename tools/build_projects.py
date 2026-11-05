#!/usr/bin/env python3
"""Copies every project in projects/ into a site folder.

    tools/build_projects.py <site dir>

Each projects/<name>/project.json lists a project's scenes. Their .loop files
(and trailer stills) go flat into <site>/scenes/, so `use` and `go_to` find
each other by name, and <site>/scenes/index.json lists the projects for the
player's scene picker and the editor. The engine never needs any project.
"""
import json
import re
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
site = Path(sys.argv[1] if len(sys.argv) > 1 else root / "site")
out = site / "scenes"
out.mkdir(parents=True, exist_ok=True)

projects, owner = [], {}
manifests = sorted((root / "projects").glob("*/project.json"), key=lambda m: (json.loads(m.read_text()).get("order", 100), m.parent.name))
for manifest in manifests:
    folder = manifest.parent
    p = json.loads(manifest.read_text())
    build = p.get("build", {})
    if not build.get("publish", True):
        continue  # Build → "Include this project on the site" is off
    stills = folder / p.get("stillsDir", p.get("stills", "stills"))
    scenes_dir = folder / build.get("scenesDir", ".")
    library_dir = folder / build.get("libraryDir", ".")
    with_stills = build.get("stills", True)
    scenes = []
    for name in p.get("scenes", []) + p.get("library", []):
        src = (library_dir if name in p.get("library", []) else scenes_dir) / f"{name}.loop"
        if not src.exists():
            sys.exit(f"{manifest}: no scene file {src.name}")
        if name in owner:
            sys.exit(f"scene name '{name}' is in both {owner[name]} and {p['name']} (names must be unique)")
        owner[name] = p["name"]
        shutil.copy(src, out / src.name)
        if name in p.get("library", []):
            continue
        m = re.search(r'^title\s+"([^"]*)"', src.read_text(), re.M)
        has_still = with_stills and (stills / f"{name}.png").exists()
        if has_still:
            shutil.copy(stills / f"{name}.png", out / f"{name}.png")
        scenes.append({"name": name, "title": m.group(1) if m else name, "still": has_still})
    # the project's other pictures (reviews, before and after) at <site>/<project>/
    if with_stills and stills.is_dir():
        for png in sorted(stills.glob("*.png")):
            if png.stem not in owner:
                (site / p["name"]).mkdir(exist_ok=True)
                shutil.copy(png, site / p["name"] / png.name)
    projects.append({
        "name": p["name"], "title": p.get("title", p["name"]),
        "description": p.get("description", ""), "start": p.get("start", scenes[0]["name"] if scenes else ""),
        "scenes": scenes, "library": p.get("library", []),
        "order": p.get("order", 100), "settings": p.get("settings", {}), "build": build,
    })

(out / "index.json").write_text(json.dumps({"projects": projects}, indent=1))
print(f"{len(projects)} projects, {sum(len(p['scenes']) for p in projects)} scenes -> {out}")
