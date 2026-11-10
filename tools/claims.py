#!/usr/bin/env python3
"""Who is working on what (work/claims/*.md), and whether two jobs would touch the same file.

    tools/claims.py             list every claim; exit 1 if two touch the same file
    tools/claims.py free FILE.. say whether these files are free to claim (exit 1 if not)

A file is matched by name or by a folder or a * pattern in a claim's `touch:` line.
"""
import fnmatch
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CLAIMS = ROOT / "work" / "claims"


def read(p):
    c = {"file": p.name}
    for line in p.read_text().splitlines():
        if ":" in line and not line.startswith(("#", " ", "`")):
            k, v = line.split(":", 1)
            c[k.strip()] = v.split("#")[0].strip()
    c["touches"] = [t.strip() for t in c.get("touch", "").split(",") if t.strip()]
    return c


def claims():
    return [read(p) for p in sorted(CLAIMS.glob("*.md")) if p.name != "README.md"]


def overlap(a, b):
    def m(x, y):
        return x == y or fnmatch.fnmatch(x, y) or fnmatch.fnmatch(y, x) or x.startswith(y.rstrip("/") + "/") or y.startswith(x.rstrip("/") + "/")
    return sorted({x for x in a for y in b if m(x, y)})


def main():
    cs = [c for c in claims() if c.get("status") not in ("deployed",)]
    if len(sys.argv) > 2 and sys.argv[1] == "free":
        busy = [(c["code"], overlap(sys.argv[2:], c["touches"])) for c in cs]
        busy = [(code, f) for code, f in busy if f]
        for code, f in busy:
            print(f"taken by {code}: {', '.join(f)}")
        print("free" if not busy else "not free")
        return 1 if busy else 0
    bad = 0
    for c in cs:
        print(f"{c.get('code', '?'):12} {c.get('status', '?'):9} {c.get('hand', '?'):12} {c.get('title', '')}")
    for i, a in enumerate(cs):
        for b in cs[i + 1:]:
            f = overlap(a["touches"], b["touches"])
            if f:
                bad = 1
                print(f"CLASH {a.get('code')} and {b.get('code')}: {', '.join(f)}")
    print(f"{len(cs)} jobs claimed" + ("" if not bad else ", with clashes"))
    return bad


if __name__ == "__main__":
    sys.exit(main())
