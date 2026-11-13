# Fluff ↔ Claude Code — shared notebook

**Where it lives in the repo:** `work/FLUFF-CLAUDE.md`
**Who reads it:** Fluff (Muse, the requester) and Claude Code (the hands). Joshua talks only to Fluff, in plain words. He never has to copy-paste between us. Claude Code just opens the repo and reads this file first.

**Roles (Joshua, 2026-11-13):** Fluff is just the requester — he brings Joshua's request in plain words and puts the job here. Fable (Claude Fable 5.2, Top tier) still does the planning/design — per `docs/MODELS.md`, Top writes the order cards, judges pictures, does hard maths/reviews/the plan. Sonnet 5.5 (Workhorse) carries out a written order; Opus 5.5 (Strong) does most engine work/merging/debugging.

## How it works

1. **Fluff writes a job here.** One job at a time, in plain words, with the files to touch, the exact change, and the proof that must pass.
2. **Claude Code reads this file first, every time.** Then it does only that job, on its own branch.
3. **Claude Code writes its report back here, under the job.** What it changed, the proof output, what Joshua should look at on his phone, anything odd, anything left undone. No "done" without proof.
4. **Fluff reads the report, checks it against the repo, and tells Joshua in plain words** what is actually done and what to look at.
5. **Joshua says go / change it / stop.** Fluff writes the next job here.

Rules, from the repo's own rulebook (`CLAUDE.md`, `docs/NORTH.md`, `docs/WORK_ORDERS.md`):
- One job, one outcome, one part. If it needs two parts, it is two jobs. Job 2 below is one bundled job, at Joshua's word: 10 small pieces in one shot, then fix only the 1-2 that aren't perfect.
- Stay inside the circle Joshua drew. Structure first, polish last.
- Never touch `PLAN.md` or `LOG.md` — those are the deployer's.
- Never merge to `main` — only the deployer does that.
- If a job needs a file it was not given, or a decision that is not in the job, stop and write that here. Do not guess past it.

This notebook is the simple version of the repo's existing board (`work/claims/`, `work/HANDOFF.md`, `docs/CONDUCTOR.md`). The board stays for many agents at once. This notebook is for the Fluff ↔ Claude Code loop, one job at a time.

---

## Job 1 — TEST: the 67th bone (status: done on branch, not merged)

**Goal, in plain words:** Add one bone to the human skeleton, so it goes from 66 bones to 67, and Joshua can see it on his phone in "The skeleton" scene.

**The bone:** coccyx — the tailbone, under the hips.

**Report (relayed by Joshua, 2026-11-13):** Edits are in. Skeleton now has 67 bones. Pushed to `claude/amazing-goodall-lnt9ta`, not merged to `main`. Skeleton tests pass, `loop check projects/samples/skeleton.loop` passes. 4 failed checks in the library picture test and 1 in the editor test were already broken before this job (checked with changes stashed). The `CLAUDE.md` trim from Claude's first answer is committed on that same branch — say so and it will be reverted. Claim step skipped, this was the only chat working. Joshua does not need to test it for now; keep it as is on that branch.

---

## Job 2 — The Bar, reconsolidated, with the new lighting (status: waiting for Claude Code)

**Goal, in plain words:** Take everything needed from the old bar and bring it together in one place in LOOP3D, before adding more. The bar should feel like Joshua's bar again — same games, same life, same lived-in feel — but with the new LOOP3D lighting on top. The new lighting is the rule: it is really good, use it everywhere, no old fake fill.

**Read first:** `CLAUDE.md`, `docs/NORTH.md` §4, `projects/bar3d/TRANSFER.md`, `docs/MODELS.md` (Fable plans first, then Sonnet builds).

**Do these 10, in one shot, then fix only what isn't perfect:**
1. **New lighting only.** LOOP3D light everywhere in the bar. Light only from real light sources. Fix the dark oval patch on the hall floor in cinema stills. Lamps breathe, buzz, and thump to music per the rig; the switch by the door dims the bar to 25%.
2. **Bar layout, as it was.** `projects/bar3d/` — 507 things, 167 models, 3 rooms. Fix the hitboxes: stools are separate things you can sit on, not connected, no huge boxes. Walls, furniture and raised floors block you where they should, and nowhere else.
3. **Scripts over, formatted right.** Bring the old bar's scripts over, formatted correctly to Looped Script. Button it up, don't invent new behaviour.
4. **Games, in the batch:** darts, chess, piano, jukebox, 4K Mania, racing sim (still new — finish it to playable), bartender job. Each one playable on the phone.
5. **Clickable things:** sit on seats, click tables, click lamps. Light switch works.
6. **Multiplayer:** lobby, player cards, accounts, Cloudflare room — keep the existing room protocol in 3D `cloud/`, don't rebuild it. Voice over WebRTC, same age groups apart, as in the old bar.
7. **Avatars, consolidated:** the old walking avatars, consolidated into one really good one. It walks, breathes, looks at whoever talks, feet planted. Use the engine walker and the 67-bone skeleton.
8. **Books, mirror, fireplace:** bookshelf with readable books (Dostoevsky, Einstein, and the rest), big mirror where you see yourself, fire that crackles in the fireplace.
9. **Arcade platformer:** Joshua's first project, bring it over playable in the arcade.
10. **Music:** jukebox and piano music, sample playback, strings dancing to the music. If sample playback is not ready, say so in the report and leave it undone — do not fake it.

**Proof:**
```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/loop_tests && node tests/editor_source_test.js && node tests/recorder_test.js
./build/loop check projects/bar3d/bar.loop
```
All must pass, except the pre-existing failures already named in Job 1 (4 library picture, 1 editor) — list them separately if they are still failing, do not hide them. Then open the bar in the player and walk it: sit, click a lamp, play piano, throw a dart, look in the mirror.

**Not:** No Loft work, no new rooms, no switch to live yet, no `PLAN.md` / `LOG.md` changes, no merge to `main`, no IONOS / Cloudflare move (Phase D, put off).

**To start this job in Claude Code, Joshua only has to say:** "Read `work/FLUFF-CLAUDE.md` first, then do Job 2. Fable plans first, then Sonnet builds."

---

## Claude Code's report — Job 2

*Claude Code, write here when Job 2 is done, or stuck:*

- Status: _waiting_
- What changed, file by file:
- Proof command and its last lines:
- What Joshua should look at on his phone:
- Anything odd / left undone:
