# Many agents at once

How several agents work on Loop3D at the same time without getting in each other's way, and how finished work
is shipped while unfinished work keeps going. Written 2026-11-10 on the user's word: "a lot of agents on
different parts instead of waiting so much", and "a deploy agent … keeping what's currently worked on in the works".

## The idea in four lines
1. **Every job has its own branch** (`work/<code>`) and its own copy of the code. Nothing an agent does touches
   `main` or anyone else's work.
2. **Every job is claimed on `main`** in `work/claims/<code>.md`: who, which files, and its status. Two jobs may
   never claim the same file; `python3 tools/claims.py` refuses it.
3. **When a job is done,** its agent pushes its branch and marks its claim `done`. That's all.
4. **The deployer ships:** it merges every `done` job into `main`, one at a time, each checked on top of the
   latest `main`, and leaves every `working` job alone. Run it whenever you like, as often as you like.

## What you say in the chat (no commands to learn)

| You say | What happens |
|---|---|
| "Start lanes 1, 3 and 4" | The lead claims those jobs on `main` and starts one agent per lane, each in its own copy, in the background. You keep chatting. |
| "Start everything that's ready" | Every lane below that doesn't clash with another, all at once. |
| "How are the agents doing?" | The lead lists the claims: working, done, stuck. |
| "Deploy" (or "ship what's done") | The deployer merges every finished job that passes its checks, pushes `main`, logs each one. Jobs still working stay as they are. |
| "Deploy and publish" | The same, then the public player is updated and checked. |
| "Make the next ten library entries" | Ten asset-smiths, one entry each, each judged; the deployer ships the ones that pass. |
| "Open a session for lane 2" | A separate cloud session on its own machine, for a long job you want to watch on its own; it pushes its branch and claim like any agent. |

**Two ways to run an agent:**
- *In this chat, in the background:* best for most orders. The lead hears back when each finishes and can start
  the next one at once. Several can run at the same time.
- *As its own cloud session:* best for long jobs (an hour and more) or to watch one job closely. Each session has
  its own machine and can be followed from the Claude app.

How many at once: as many as there are lanes that don't clash. Four to six is a sensible start; the limit is your
plan's usage, not the engine.

## The job's life

```
lead writes the claim (status: working) on main → agent works on work/<code> in its own copy
→ proof passes → claim status: done, branch pushed → deployer: merge onto latest main, checks
→ pass: on main, claim removed, LOG.md line        → fail or conflict: claim status: stuck, branch kept
```

A `stuck` job goes back to the lead: either the order was cut wrong (it needed a file another job holds, or a
design decision), or main moved under it. The lead re-cuts it or re-runs it on the new `main`.

## The lanes ready now (2026-11-10)

Each is a full card in `docs/WORK_ORDERS.md` (or below). None touches the same files as another, so all can run
at once. The tier is from `docs/MODELS.md`.

| Lane | Job | Touches | Agent | Tier |
|---|---|---|---|---|
| 1 | **E4** · a word's test in one line | `tests/test.h`, `tests/words_test.cpp` | builder | Workhorse |
| 2 | **E5** · `loop library picture`: a picture of one entry | `tools/loop.cpp` | builder | Workhorse |
| 3 | **E6** · the library card's new lines | `engine/project/library.*`, `tests/library_test.cpp`, `library/` | builder | Workhorse |
| 4 | **E7** · the rubric files and the judge's sheet | `docs/rubrics/`, `perf/judge.md` | lead | Top |
| 5 | **G1** · why the Glade's picture test differs (below) | `tests/pictures/showcase_glade.png`, `tools/picture_check.js` | builder | Strong |
| 6 | **M1** · the design of a surface recipe | `docs/SURFACES.md` | lead | Top |

After lane 1 is deployed: **S1** (`lathe`), then S2 to S4, one at a time because they share
`engine/script/words_shapes.cpp`. After lanes 2 and 3: library entries can start, as many at once as wanted
(each touches only its own file).

```
G1 · Why the Glade's picture differs                               hand: builder   size: S
Read:   tools/picture_check.js, tests/pictures/scenes.json, LOG.md lines of 2026-11-10 (the leaves and shadow fixes)
Touch:  tests/pictures/showcase_glade.png, tools/picture_check.js
Do:     1. Run `xvfb-run -a node tools/picture_check.js showcase_glade` three times; is the difference the same
           each time (a stale reference) or does it move (a moment that isn't pinned)?
        2. Look at build/pictures/showcase_glade.png, its _diff.png and the reference. Name what differs.
        3. If it is the leaf and shadow fixes of 2026-11-10 (an intended change): update only that reference
           (`--update showcase_glade`) and say so. If it moves between runs: report it, change nothing.
Proof:  the picture test passes 12 of 12, or the report shows three runs' numbers and the diff picture.
```

## Different chats on different models
Each chat has its own model (`/model` in that chat) and its own machine, so a Fable chat and an Opus chat can work
at the same time. They meet only through `main`: the claims say who holds which files, and the deployer ships
what is done. A claim is pushed to `main` before any work starts, so the second chat to claim a file is refused.
What can still go wrong: a job that needs another job's unfinished work must wait for it to be deployed; and a
claim that was too narrow (the job needed a file it didn't list) comes back `stuck`, to be re-claimed.

## Rules every parallel agent keeps
- Read its claim first; touch only its claim's files; stop if another claim holds a file it needs.
- Work only on its own branch; push only its own branch; never `main`.
- Mark the claim `done` only with the proof's output in hand; `stuck` with the reason otherwise.
- The deployer alone merges to `main`, and never force-pushes.
- `LOG.md` and `PLAN.md` belong to the deployer: a job writes its log line in its claim's `note:`.
