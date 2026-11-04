# Performance data

Every benchmark run of the Loop3D player, kept as data.

- `raw/<run id>.json`: each run whole, as the player's **Copy results** gives it.
- `runs.json`: one row per run (its summary).
- `segments.json`: one row per scene of every run.

Add runs with `tools/perf_add.py result.json`. It rebuilds the two tables, which feed the Smoothness Tracker. What each number means is in [`docs/TUNING.md`](../docs/TUNING.md), section 5.

Runs on "SwiftShader (software, test machine)" come from the headless browser on Claude's build machine. They check that recording works; their numbers don't describe a real GPU.
