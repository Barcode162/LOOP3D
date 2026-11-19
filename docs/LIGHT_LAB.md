# The light lab

The owner, 2026-11-19: the Loft's light "isn't how I like it… I want something fully different, and performance is
already struggling", toward the feel of Unreal's Lumen even by another technique. "Become a scientist: research,
get real test results, and a sheet we update every time."

The sheet: **Light Lab** (claude.ai/artifact/9a9k5xp31ACbTQysgTcLQv), fed from `perf/lab/results.json` and
`perf/lab/methods.json`. Every experiment is one row; nothing is called better without a number.

## The two numbers (every experiment gets both)
1. **Closeness to the film picture**: `loop score` (ΔE, 0 the same, about 2 barely seen, 10+ plainly different)
   between the live picture and the path tracer's picture of the same view. The film picture is the ceiling: no
   live method can look better than the same light done perfectly.
2. **Cost on the phone**: `loop budget` (lamp work per pixel, budget 24) and, once a method is on the player,
   milliseconds timed on the iPhone (the recording page). The lab machine's GPU is software, so its milliseconds
   are not the phone's and are not recorded.

A third question sits beside them: **does the film picture itself look the way the owner wants?** That is the
look (where the lamps are, exposure, tone), judged by the owner's eye, not a number. Kept apart, so a better
method isn't blamed for a lighting design, or the other way round.

## How to run one
`xvfb-run -a node tools/light_lab.js <scene> <name> "<switches>"` (needs `site/`): it settles the live picture as
the picture tests do, reads it back from the GPU, scores it against the film picture (made once per scene, about
8 minutes for the Loft on 4 cores, then reused), and prints one JSON line. `QUALITY=1` High, `QUALITY=2` Ultra.
`./build/loop budget <scene> --set name=value` for the cost.

## The methods (level 1: ours; level 2: the best that exists; level 3: what nobody has done)
See `perf/lab/methods.json` for the list the sheet shows, with each one's verdict as experiments land.
