# The board: who is working on what

One file per job, named by its order code (`E4.md`, `L-cholla.md`), made and changed only with
`python3 tools/claims.py` (`new`, `beat`, `finish`; see `docs/CONDUCTOR.md`). A claim is written on `main` before
work starts, so every chat can see it; the job's later check-ins go on its own branch, and `--remote` reads them.
Two claims may never hold the same file. The deployer removes a claim when the job is on `main`.

```
code:     E4
title:    A word's test in one line
hand:     builder
size:     S
wave:     w1
branch:   work/E4
status:   working          (working | waiting | done | stuck | deployed)
touch:    tests/test.h, tests/words_test.cpp
depends:  (codes this job waits for)
pictures: no               (yes: the deployer also runs the 12 picture tests)
expected: 25               (minutes, learnt from perf/jobs.md)
started:  2026-11-10T21:05Z
updated:  2026-11-10T21:20Z
note:     the last check-in, or the report's first line
```
