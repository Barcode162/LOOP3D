#!/usr/bin/env python3
"""The Loop3D Workshop dashboard's data, from the project itself.

    tools/dashboard.py [out dir]      (default build/dashboard/)

Writes one JSON file per dataset (rows as objects):
  parts.json    the engine map (engine/modules.json) with each part's lines and size
  growth.json   the engine's lines per side and the tests, commit by commit (git)
  pieces.json   the bar, piece by piece (projects/bar/pieces.json)
  gaps.json     what the engine is missing (engine/gaps.json)
  phone.json    benchmark runs on the user's iPhone (perf/segments.json)
  budget.json   the work budget's latest table (perf/budget.md)
  library.json  the library's entries (library/: name, kind, what, where it came from)
  summary.json  the headline figures, one row each
Run it after `cmake --build build` (the test count) and tools/build_site.sh
(sizes in the web build); the dashboard is then updated with these files.
"""
import fnmatch
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "dashboard"
PARTS = json.loads((ROOT / "engine/modules.json").read_text())["parts"]


def part_of(path):
    for p in PARTS:
        if any(fnmatch.fnmatch(path, g) for g in p["files"]):
            return p
    return None


def git(*args, binary=False):
    out = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, check=True).stdout
    return out if binary else out.decode()


def size_kb(path):
    """Compiled size for C++ (the web build's object), file size otherwise."""
    if path.endswith(".cpp"):
        obj = ROOT / "build-web/CMakeFiles/loop3d.dir" / (path + ".o")
        if not obj.exists():
            obj = ROOT / "build-web/CMakeFiles/player.dir" / (path + ".o")
        return obj.stat().st_size / 1024 if obj.exists() else 0
    if path.endswith((".h", ".hpp")):
        return 0  # compiled into the files that include it
    return (ROOT / path).stat().st_size / 1024


def lines(text):
    return text.count(b"\n") if isinstance(text, bytes) else text.count("\n")


tracked = git("ls-files").split()
rows = []
for p in PARTS:
    files = [f for f in tracked if part_of(f) is p]
    n = sum(lines((ROOT / f).read_bytes()) for f in files)
    kb = sum(size_kb(f) for f in files)
    rows.append({"part": p["name"], "side": p["side"], "status": p["status"], "what": p["what"],
                 "files": len(files), "lines": n, "kb": round(kb, 1)})
parts = rows

# ---- growth: every commit's lines per side and its tests
commits = [l.split(" ", 1) for l in git("log", "--reverse", "--format=%H %aI").splitlines()]
growth = []
batch = subprocess.Popen(["git", "cat-file", "--batch"], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)


def blob(spec):
    batch.stdin.write((spec + "\n").encode())
    batch.stdin.flush()
    head = batch.stdout.readline().split()
    if len(head) < 3 or head[1] != b"blob":
        return b""
    data = batch.stdout.read(int(head[2]))
    batch.stdout.read(1)
    return data


for sha, at in commits:
    files = git("ls-tree", "-r", "--name-only", sha).split()
    side = {"core": 0, "visual": 0, "tools": 0}
    tests = 0
    for f in files:
        if f.startswith("tests/") and f.endswith(".cpp"):
            tests += len(re.findall(rb"^TEST\(", blob(f"{sha}:{f}"), re.M))
            continue
        p = part_of(f)
        if p:
            side[p["side"]] += lines(blob(f"{sha}:{f}"))
    if sum(side.values()):
        growth.append({"at": at, "commit": sha[:7], **side, "tests": tests})
batch.stdin.close()

pieces = json.loads((ROOT / "projects/bar/pieces.json").read_text())["pieces"]
gaps = json.loads((ROOT / "engine/gaps.json").read_text())["gaps"]
for g in gaps:
    g.setdefault("closed", "")

# ---- the phone's runs
phone = []
for r in json.loads((ROOT / "perf/segments.json").read_text()):
    if "swiftshader" in r.get("gpu", "").lower():
        continue
    phone.append({"scene": r["scene"], "title": r.get("title", r["scene"]), "fps": round(r["fps"], 1), "gpu_ms": round(r["gpuMs"], 1),
                  "res": round(r["scaleAvg"] * 100), "hitches": r["hitches"], "low1_fps": round(r["low1Fps"], 1),
                  "when": r.get("when", ""), "build": r.get("build", "")})

# ---- the work budget's latest table
md = (ROOT / "perf/budget.md").read_text()
last = md.split("```")[-2] if md.count("```") >= 2 else ""
budget = []
for line in last.splitlines():
    m = re.match(r"^(\w+)\s+(\d+) > (\d+)\s+([\d.]+) > ([\d.]+)\s+(\d+) > (\d+)\s+([\d.]+)\s+(\d+) > ([\d.]+)\s+(\d+)\s+(.*)$", line)
    if m:
        verdict = m.group(12).strip()
        budget.append({"scene": m.group(1), "triangles_m": float(m.group(4)), "draws": int(m.group(6)), "shaded": float(m.group(8)),
                       "lamps": int(m.group(9)), "lamp_work": int(m.group(11)), "fits": verdict == "fits", "over_on": "" if verdict == "fits" else verdict})

# ---- the library: every entry (library/<kinds>/<name>.loop), read by the engine itself
lib_out = subprocess.run([str(ROOT / "build/loop"), "library", "json"], cwd=ROOT, capture_output=True, text=True)
library = [{"name": e["name"], "kind": e["kind"], "what": e["what"], "from": e["from"]} for e in (json.loads(lib_out.stdout) if lib_out.returncode == 0 else [])]

# ---- headline figures
test_out = subprocess.run([str(ROOT / "build/loop_tests")], cwd=ROOT, capture_output=True, text=True).stdout
m = re.search(r"(\d+) tests, (\d+) failed checks", test_out)
tests_now, failed = (int(m.group(1)), int(m.group(2))) if m else (0, -1)
by = lambda side, key: sum(p[key] for p in parts if p["side"] == side)
core_lines = by("core", "lines")
core_in_place = sum(p["lines"] for p in parts if p["side"] == "core" and p["status"] == "in place")
wasm = ROOT / "site/player.wasm"
summary = [
    {"metric": "tests", "value": tests_now},
    {"metric": "failed_checks", "value": failed},
    {"metric": "core_lines", "value": core_lines},
    {"metric": "visual_lines", "value": by("visual", "lines")},
    {"metric": "tools_lines", "value": by("tools", "lines")},
    {"metric": "core_kb", "value": round(by("core", "kb"))},
    {"metric": "visual_kb", "value": round(by("visual", "kb"))},
    {"metric": "core_in_place_share", "value": round(core_in_place / core_lines, 3) if core_lines else 0},
    {"metric": "parts_to_separate", "value": sum(1 for p in parts if p["status"] != "in place")},
    {"metric": "player_kb", "value": round(wasm.stat().st_size / 1024) if wasm.exists() else 0},
    {"metric": "pieces_done", "value": sum(1 for p in pieces if p["status"] == "done")},
    {"metric": "pieces_total", "value": len(pieces)},
    {"metric": "gaps_open", "value": sum(1 for g in gaps if g["status"] == "open")},
    {"metric": "gaps_closed", "value": sum(1 for g in gaps if g["status"] == "closed")},
    {"metric": "scenes_over_budget", "value": sum(1 for b in budget if not b["fits"])},
    {"metric": "scenes_measured", "value": len(budget)},
    {"metric": "phone_runs", "value": len(phone)},
    {"metric": "library_entries", "value": len(library)},
]

OUT.mkdir(parents=True, exist_ok=True)
for name, data in [("parts", parts), ("growth", growth), ("pieces", pieces), ("gaps", gaps), ("phone", phone), ("budget", budget), ("library", library), ("summary", summary)]:
    (OUT / f"{name}.json").write_text(json.dumps(data, indent=1))
    print(f"{name}.json: {len(data)} rows")
