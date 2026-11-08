#!/usr/bin/env python3
"""Adds benchmark results from the Loop3D player to perf/.

    tools/perf_add.py result.json [more.json ...]   add runs (a file may hold one run or a list)
                                                    (the recording page's rows too: bench "live",
                                                    each scene in parts of 30 s)
    tools/perf_add.py                               just rebuild the tables from perf/raw/

Every run is kept whole in perf/raw/<run id>.json. From those it rebuilds:
  perf/runs.json      one row per run: the summary
  perf/segments.json  one row per scene of every run
These two tables are what the Smoothness Tracker charts.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent / "perf"
RAW = ROOT / "raw"


def gpu_name(dev):
    g = dev.get("gpu") or {}
    name = (g.get("description") or "").strip() or " ".join(x for x in (g.get("vendor"), g.get("architecture")) if x).strip()
    if "swiftshader" in name.lower():
        return "SwiftShader (software, test machine)"
    return name or "unknown GPU"


def run_id(r):
    if r.get("id"):  # a live recording's row (the recording page) names itself
        return r["id"]
    stamp = re.sub(r"[^0-9T]", "", r["when"][:19]).replace("T", "-")
    return f"{stamp}-{r.get('quality', 'q')}-{r.get('start', 'scene')}"


def add(path):
    data = json.loads(pathlib.Path(path).read_text())
    runs = data if isinstance(data, list) else [data]
    for r in runs:
        if r.get("kind") != "loop3d-bench":
            print(f"skipped something that isn't a Loop3D benchmark in {path}")
            continue
        if not r.get("segments"):  # a live visit that only reported trouble (its events): nothing to time
            print(f"skipped {r.get('id') or path}: no frames recorded" + (f" ({len(r.get('events', []))} events)" if r.get("events") else ""))
            continue
        rid = run_id(r)
        RAW.mkdir(parents=True, exist_ok=True)
        (RAW / f"{rid}.json").write_text(json.dumps(r, indent=1))
        print(f"kept {rid}")


def rebuild():
    runs, segs = [], []
    for f in sorted(RAW.glob("*.json")):
        r = json.loads(f.read_text())
        rid, dev, s = f.stem, r.get("device", {}), r.get("summary", {})
        base = {"runId": rid, "when": r["when"], "day": r["when"][:10], "quality": r.get("quality"), "gpu": gpu_name(dev), "build": r.get("build", "")}
        runs.append({**base, "bench": r.get("bench"), "start": r.get("start"), "scenes": s.get("scenes"), "seconds": s.get("seconds"),
                     "frames": s.get("frames"), "fps": s.get("fps"), "worstLow1Fps": s.get("worstLow1Fps"), "worstP99Ms": s.get("worstP99Ms"),
                     "maxMs": s.get("maxMs"), "hitches": s.get("hitches"), "avgScale": s.get("avgScale"), "worstLoadMs": s.get("worstLoadMs"),
                     "dpr": dev.get("dpr"), "screen": dev.get("screen"), "browser": browser(dev.get("ua", ""))})
        for x in r.get("segments", []):
            segs.append({**base, "scene": x.get("scene"), "title": x.get("title") or x.get("scene"), "mode": x.get("mode"),
                         "part": x.get("part"), "quality": x.get("quality") or base["quality"],
                         **{k: x.get(k) for k in ("seconds", "frames", "fps", "avgMs", "p50Ms", "p95Ms", "p99Ms", "maxMs", "low1Fps", "low01Fps",
                                                  "hitches", "hitchPct", "dropped", "cpuMs", "cpuP99Ms", "gpuMs", "gpuP95Ms", "scaleAvg",
                                                  "scaleMin", "loadMs", "things", "lights", "width", "height")}})
    # a negative load time came from an early build that mixed two clocks
    for row in runs:
        if (row.get("worstLoadMs") or 0) < 0:
            row["worstLoadMs"] = None
    for row in segs:
        if (row.get("loadMs") or 0) < 0:
            row["loadMs"] = None
    runs.sort(key=lambda r: r["when"])
    segs.sort(key=lambda r: (r["when"], r["scene"] or ""))
    ROOT.mkdir(exist_ok=True)
    (ROOT / "runs.json").write_text(json.dumps(runs, indent=1))
    (ROOT / "segments.json").write_text(json.dumps(segs, indent=1))
    print(f"{len(runs)} runs, {len(segs)} scene rows")


def browser(ua):
    for name, pat in (("Edge", r"Edg/(\d+)"), ("Chrome", r"Chrome/(\d+)"), ("Safari", r"Version/(\d+).*Safari"), ("Firefox", r"Firefox/(\d+)")):
        m = re.search(pat, ua)
        if m:
            return f"{name} {m.group(1)}" + (" (headless)" if "Headless" in ua else "")
    return "browser"


if __name__ == "__main__":
    for p in sys.argv[1:]:
        add(p)
    rebuild()
