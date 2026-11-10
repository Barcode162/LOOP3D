#!/usr/bin/env python3
"""The board: who is working on what (work/claims/*.md), how long it is taking, what waits on what.

Every agent, in any chat, reads and writes the same board, so they can tell each other when they start, how
they are getting on, when they are done, and what they are waiting for. A claim on main is the job's start;
the agent's own branch (work/<code>) carries its later check-ins, and this tool reads both.

    tools/claims.py                    the board: every job, its status, time taken against what was expected
    tools/claims.py free FILE..        are these files free to claim? (exit 1 if not)
    tools/claims.py new CODE TITLE --hand H --touch "a, b" [--wave W] [--expected MIN] [--depends "X, Y"] [--pictures yes]
                                       write a claim (refused if another claim holds a file)
    tools/claims.py beat CODE [STATUS] [NOTE]
                                       a check-in: the time now, and a new status or note (working, done, stuck, waiting)
    tools/claims.py wave W             is every job of wave W done? (exit 0 yes, 1 no, 2 something stuck)
    tools/claims.py ready              claimed jobs whose `depends:` are all deployed or done
    tools/claims.py overdue            jobs past 1.5x their expected time, or silent for 30 minutes
    tools/claims.py finish CODE        record a deployed job's real time in perf/jobs.md (the deployer)
    tools/claims.py expect SIZE HAND   the usual time for a job of this size and hand, learnt from perf/jobs.md

`--remote` (any command) also reads each job's branch on origin, so check-ins not yet on main count.
"""
import datetime as dt
import fnmatch
import pathlib
import re
import statistics
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CLAIMS = ROOT / "work" / "claims"
JOBS = ROOT / "perf" / "jobs.md"
FIELDS = ["code", "title", "hand", "size", "wave", "branch", "status", "touch", "depends", "pictures",
          "expected", "started", "updated", "finished", "note"]
# a first guess (minutes) until perf/jobs.md has real times for this size and hand
GUESS = {"S": 25, "M": 60, "L": 120}
QUIET = 30  # minutes without a check-in before a working job counts as silent


def now():
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0)


def stamp(t):
    return t.strftime("%Y-%m-%dT%H:%MZ")


def parse_time(s):
    try:
        return dt.datetime.strptime(s, "%Y-%m-%dT%H:%MZ").replace(tzinfo=dt.timezone.utc)
    except (TypeError, ValueError):
        return None


def parse(text, name="?"):
    c = {"file": name}
    for line in text.splitlines():
        if ":" in line and not line.startswith(("#", " ", "`")):
            k, v = line.split(":", 1)
            c[k.strip()] = v.split("  #")[0].strip()
    c["touches"] = [t.strip() for t in c.get("touch", "").split(",") if t.strip()]
    c["needs"] = [t.strip() for t in c.get("depends", "").split(",") if t.strip()]
    return c


def write(c):
    lines = [f"{k + ':':9} {c[k]}" for k in FIELDS if c.get(k)]
    (CLAIMS / f"{c['code']}.md").write_text("\n".join(lines) + "\n")


def git(*a):
    r = subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def claims(remote=False):
    out = {}
    for p in sorted(CLAIMS.glob("*.md")):
        if p.name != "README.md":
            c = parse(p.read_text(), p.name)
            out[c.get("code", p.stem)] = c
    if remote:
        git("fetch", "-q", "origin", "+refs/heads/work/*:refs/remotes/origin/work/*")
        for code, c in list(out.items()):
            branch = c.get("branch") or f"work/{code}"
            t = git("show", f"origin/{branch}:work/claims/{code}.md")
            if t:
                r = parse(t, c["file"])
                if (r.get("updated") or "") >= (c.get("updated") or ""):
                    out[code] = r
    return list(out.values())


def overlap(a, b):
    def m(x, y):
        return (x == y or fnmatch.fnmatch(x, y) or fnmatch.fnmatch(y, x)
                or x.startswith(y.rstrip("/") + "/") or y.startswith(x.rstrip("/") + "/"))
    return sorted({x for x in a for y in b if m(x, y)})


def minutes(c, until=None):
    s = parse_time(c.get("started"))
    e = parse_time(c.get("finished")) or until or now()
    return None if not s else round((e - s).total_seconds() / 60)


def history():
    rows = []
    if JOBS.exists():
        for line in JOBS.read_text().splitlines():
            m = re.match(r"\|\s*(\S+)\s*\|\s*(\S*)\s*\|\s*(\S*)\s*\|\s*(\d*)\s*\|\s*(\d+)\s*\|", line)
            if m and m.group(1) != "code":
                rows.append({"code": m.group(1), "size": m.group(2), "hand": m.group(3),
                             "expected": int(m.group(4) or 0), "took": int(m.group(5))})
    return rows


def expect(size, hand):
    same = [r["took"] for r in history() if r["size"] == size and r["hand"] == hand]
    if len(same) >= 3:
        return round(statistics.median(same))
    return GUESS.get(size, 45)


def late(c):
    """Why a working job needs a look, or None."""
    if c.get("status") not in ("working", "waiting"):
        return None
    took, exp = minutes(c), int(c.get("expected") or 0)
    if took is not None and exp and took > 1.5 * exp:
        return f"{took} min against {exp} expected"
    u = parse_time(c.get("updated"))
    if u and (now() - u).total_seconds() > QUIET * 60:
        return f"no check-in for {round((now() - u).total_seconds() / 60)} min"
    return None


def board(cs):
    bad = 0
    print(f"{'job':10} {'wave':6} {'status':9} {'time':>11}  {'hand':11} title")
    for c in cs:
        took, exp = minutes(c), c.get("expected") or "?"
        t = f"{took if took is not None else '-'}/{exp} min"
        flag = late(c)
        print(f"{c.get('code', '?'):10} {c.get('wave', '-'):6} {c.get('status', '?'):9} {t:>11}  "
              f"{c.get('hand', '?'):11} {c.get('title', '')}{'   << ' + flag if flag else ''}")
        if c.get("note"):
            print(f"{'':10} {c['note']}")
    live = [c for c in cs if c.get("status") != "deployed"]
    for i, a in enumerate(live):
        for b in live[i + 1:]:
            f = overlap(a["touches"], b["touches"])
            if f:
                bad = 1
                print(f"CLASH {a.get('code')} and {b.get('code')}: {', '.join(f)}")
    print(f"{len(cs)} jobs on the board" + (", with clashes" if bad else ""))
    return bad


def main(argv):
    remote = "--remote" in argv
    argv = [a for a in argv if a != "--remote"]
    cmd = argv[1] if len(argv) > 1 else "board"
    opts, rest = {}, []
    it = iter(argv[2:])
    for a in it:
        if a.startswith("--"):
            opts[a[2:]] = next(it, "")
        else:
            rest.append(a)
    cs = claims(remote)

    if cmd == "board":
        return board(cs)
    if cmd == "free":
        busy = [(c["code"], overlap(rest, c["touches"])) for c in cs if c.get("status") != "deployed"]
        busy = [(code, f) for code, f in busy if f]
        for code, f in busy:
            print(f"taken by {code}: {', '.join(f)}")
        print("free" if not busy else "not free")
        return 1 if busy else 0
    if cmd == "new":
        code, title = rest[0], " ".join(rest[1:])
        touch = [t.strip() for t in opts.get("touch", "").split(",") if t.strip()]
        if not touch:
            print("a claim needs --touch (the files it may change)")
            return 1
        busy = [(c["code"], overlap(touch, c["touches"])) for c in cs if c.get("status") != "deployed" and c.get("code") != code]
        busy = [(k, f) for k, f in busy if f]
        if busy:
            for k, f in busy:
                print(f"refused: {k} holds {', '.join(f)}")
            return 1
        size, hand = opts.get("size", "S"), opts.get("hand", "builder")
        c = {"code": code, "title": title, "hand": hand, "size": size, "wave": opts.get("wave", ""),
             "branch": opts.get("branch", f"work/{code}"), "status": "working", "touch": ", ".join(touch),
             "depends": opts.get("depends", ""), "pictures": opts.get("pictures", "no"),
             "expected": opts.get("expected") or str(expect(size, hand)),
             "started": stamp(now()), "updated": stamp(now()), "note": "claimed"}
        write(c)
        print(f"claimed {code} ({c['expected']} min expected): now commit and push work/claims/{code}.md to main")
        return 0
    if cmd == "beat":
        code = rest[0]
        c = next((x for x in claims(False) if x.get("code") == code), None)
        if not c:
            print(f"no claim {code}")
            return 1
        if len(rest) > 1 and rest[1] in ("working", "done", "stuck", "waiting", "deployed"):
            c["status"] = rest[1]
            rest = rest[:1] + rest[2:]
            if c["status"] in ("done", "deployed") and not c.get("finished"):
                c["finished"] = stamp(now())
        if len(rest) > 1:
            c["note"] = " ".join(rest[1:])
        c["updated"] = stamp(now())
        write(c)
        print(f"{code}: {c['status']} ({minutes(c)} of {c.get('expected')} min) · {c.get('note', '')}")
        return 0
    if cmd == "wave":
        w = [c for c in cs if c.get("wave") == rest[0]]
        stuck = [c["code"] for c in w if c.get("status") == "stuck"]
        open_ = [c["code"] for c in w if c.get("status") not in ("done", "deployed", "stuck")]
        if not w:
            print(f"no jobs in wave {rest[0]}")
            return 1
        if stuck:
            print(f"wave {rest[0]}: stuck: {', '.join(stuck)}" + (f"; still going: {', '.join(open_)}" if open_ else ""))
            return 2
        if open_:
            print(f"wave {rest[0]}: still going: {', '.join(open_)}")
            return 1
        print(f"wave {rest[0]}: all {len(w)} done")
        return 0
    if cmd == "ready":
        state = {c.get("code"): c.get("status") for c in cs}
        for c in cs:
            if c.get("status") == "waiting" and all(state.get(n, "deployed") in ("done", "deployed") for n in c["needs"]):
                print(c["code"])
        return 0
    if cmd == "overdue":
        n = 0
        for c in cs:
            why = late(c)
            if why:
                n += 1
                print(f"{c['code']}: {why}")
        print(f"{n} need a look" if n else "all on time")
        return 1 if n else 0
    if cmd == "finish":
        c = next((x for x in cs if x.get("code") == rest[0]), None)
        if not c or minutes(c) is None:
            print(f"no started claim {rest[0]}")
            return 1
        if not JOBS.exists():
            JOBS.write_text("# How long jobs really took\n\nWritten by `tools/claims.py finish` when the deployer ships a job; "
                            "`tools/claims.py expect` learns each size's usual time from it.\n\n"
                            "| code | size | hand | expected | took | date |\n|---|---|---|---|---|---|\n")
        with JOBS.open("a") as f:
            f.write(f"| {c['code']} | {c.get('size', '')} | {c.get('hand', '')} | {c.get('expected', '')} | "
                    f"{minutes(c)} | {stamp(now())[:10]} |\n")
        print(f"{c['code']}: took {minutes(c)} min against {c.get('expected')} expected")
        return 0
    if cmd == "expect":
        print(expect(rest[0], rest[1]))
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
