# The original repo: Barcode162/3D (Bar3D and the first Looped)

The user's first and biggest project (2026-10-15 → 11-02, 639 commits, ~95k lines
of code, ~26k lines of docs). It holds the **live website and the live bar**:
https://joshuavelasco.com/bar/. LOOP3D is a fresh, from-scratch rewrite of the
engine started 2026-11-03; 3D is where the history, the user's words and the
live product are.

Absorbed 2026-11-05 from `main` at `339ac84` and branch `job/J120` at `6f7ee9b`
(the newest work, 2026-11-02, never merged to main). Re-read it any time:
```sh
GIT_LFS_SKIP_SMUDGE=1 git clone https://github.com/Barcode162/3d /home/user/barcode162/3d   # public, read-only here
```

| File | What it holds |
|---|---|
| [../user.md](../user.md) | **Who the user is and how to work with them** (applies to every repo) |
| [vision.md](vision.md) | North star, the product, the business goal, every wish still open |
| [bar3d.md](bar3d.md) | The live bar and site: rooms, games, hosting, deploys, accounts, multiplayer |
| [looped-v1.md](looped-v1.md) | The first engine's systems, and the J116 rebuild (words, one-formula light, clusters) |
| [lighting-story.md](lighting-story.md) | Every lighting attempt, what the user liked and hated, and why |
| [requests.md](requests.md) | The user's requests in order, short quotes, what each led to |
| [rules-and-process.md](rules-and-process.md) | The old Charter, the user's rules, the five books, how sessions ran |
| [lessons.md](lessons.md) | Incidents and traps already paid for (Safari, iPhone, WebGPU, CI, hosting) |
| [port-map.md](port-map.md) | What in 3D LOOP3D can reuse (code, assets, data) and what it would take |

## Timeline of the user's repos (from GitHub, 2026-11-05)
`Racing-cars`, `VST` (10-15/16) → **3D** (10-15 → 11-02) → `Think` (private,
10-27: the learning/truth-document project) → `Small` (10-29) → **ENG** (11-02:
"The Last Ember", a story game on Looped + Looped Script, J120–J122 planned a
Skyrim-sized world and one light for everywhere) → `mobile-clash-like`,
`poopy-farty` (11-02) → `Sandbox` (11-04) → **LOOP3D** (11-03 → now).
ENG, Sandbox and Think were not read; ask before reading them.
