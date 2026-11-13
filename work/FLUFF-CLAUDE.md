# Fluff ↔ Claude Code — shared notebook

**Where it lives in the repo:** `work/FLUFF-CLAUDE.md`
**Who reads it:** Fluff (Muse, the brains) and Claude Code (the hands). Joshua talks only to Fluff, in plain words. He never has to copy-paste between us. Claude Code just opens the repo and reads this file first.

## How it works

1. **Fluff writes a job here.** One job at a time, in plain words, with the files to touch, the exact change, and the proof that must pass.
2. **Claude Code reads this file first, every time.** Then it does only that job, on its own branch.
3. **Claude Code writes its report back here, under the job.** What it changed, the proof output, what Joshua should look at on his phone, anything odd, anything left undone. No "done" without proof.
4. **Fluff reads the report, checks it against the repo, and tells Joshua in plain words** what is actually done and what to look at.
5. **Joshua says go / change it / stop.** Fluff writes the next job here.

Rules, from the repo's own rulebook (`CLAUDE.md`, `docs/NORTH.md`, `docs/WORK_ORDERS.md`):
- One job, one outcome, one part. If it needs two parts, it is two jobs.
- Stay inside the circle Joshua drew. Structure first, polish last.
- Never touch `PLAN.md` or `LOG.md` — those are the deployer's.
- Never merge to `main` — only the deployer does that.
- If a job needs a file it was not given, or a decision that is not in the job, stop and write that here. Do not guess past it.

This notebook is the simple version of the repo's existing board (`work/claims/`, `work/HANDOFF.md`, `docs/CONDUCTOR.md`). The board stays for many agents at once. This notebook is for the Fluff ↔ Claude Code loop, one job at a time.

---

## Job 1 — TEST: the 67th bone (status: waiting for Claude Code)

**Goal, in plain words:** Add one bone to the human skeleton, so it goes from 66 bones to 67, and Joshua can see it on his phone in "The skeleton" scene.

**The bone:** coccyx — the tailbone, under the hips.
Why this one: it is real, it is central so it adds exactly 1 bone, it touches nothing else, and it is visible at the back of the hips. The original 67th bone was lost — nobody in the repo names it. If Joshua later names another bone, we swap this one out. This job tests the loop, not final anatomy.

**Read first:** `CLAUDE.md`, `docs/NORTH.md` §4, `docs/STUDIO.md` §2 and T-B1.

**Do:**
1. In `library/skeletons/human.loop`, after the `hips` bone, add:
   ```
   bone "coccyx", parent: "hips", from: vec(0, 0.96, 0), to: vec(0, 0.88, 0.03), bends: vec(0, 0, 1), bend: [-10 deg, 15 deg], side: [-10 deg, 10 deg], twist: [-5 deg, 5 deg]
   ```
   Update the header comment from "the 66 bones you move" to "the 67 bones you move", and note coccyx is the test 67th bone, chosen 2026-11-13.
2. In `tests/skeleton_test.cpp`: rename the 66-bone test to 67, change both `== 66` bone counts to `== 67`, add `"coccyx"` to the names checked, change `instances.size() == 66` to `== 67`. Left/right stay 28 and 28. Free bones stay 2. Rest height stays 1.75 m.
3. In `projects/samples/skeleton.loop`: change "the 66 bones you move" to "the 67 bones you move".
4. In `docs/LANGUAGE.md` line 202: change "(66 bones, 1.75 m)" to "(67 bones, 1.75 m)".
   Touch only those four files. Nothing else.

**Proof:**
```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/loop_tests && node tests/editor_source_test.js && node tests/recorder_test.js
```
All must pass. Then `./build/loop check projects/samples/skeleton.loop` must pass with no problems.

**Not:** No other bones, no rings 2 or 3, no skin, no walker change, no Loft work, no `PLAN.md` / `LOG.md` changes, no merge to `main`.

**To start this job in Claude Code, Joshua only has to say:** "Read `work/FLUFF-CLAUDE.md` first, then do Job 1."

---

## Claude Code's report — Job 1

*Claude Code, write here when Job 1 is done, or stuck:*

- Status: done, on branch `claude/amazing-goodall-lnt9ta` (not merged to main)
- What changed, file by file:
  - `library/skeletons/human.loop`: added `coccyx` after `hips`; header says 67 bones; the 2026-11-13 note is on the `from:` line.
  - `tests/skeleton_test.cpp`: test renamed to 67; both bone counts and the instance count are 67; `coccyx` added to the checked names. Left/right 28/28, free 2 and height 1.75 m unchanged.
  - `projects/samples/skeleton.loop`: "67 bones you move".
  - `docs/LANGUAGE.md` line 202: "(67 bones, 1.75 m)".
- Proof: build passes; all 9 skeleton tests ok (`skeleton_human_has_67_bones_each_with_one_name` included); recorder 36 checks, 0 failed; `loop check projects/samples/skeleton.loop` says "reads fine".
  NOT all green: `loop_tests` shows 4 failed checks (`library_picture_of_a_surface_is_the_test_ball`, library_test.cpp:161-164) and `editor_source_test` shows 1 failed ("the engine refuses the edited script"). I re-ran both with my changes stashed and got the same 4 and 1, so they were already failing before this job.
- What Joshua should look at on his phone: "The skeleton" scene, a small bone at the back of the hips. Not looked at on a phone: the player was not rebuilt or published.
- Anything odd / left undone: the repo's `build/` folder was made with Makefiles, so I built in a scratch folder instead. Also, before I read this file I trimmed `CLAUDE.md` by mistake (commit already pushed on this branch); Joshua can say if he wants it reverted.
