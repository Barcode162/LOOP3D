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

*Written 2026-11-13 by Claude (Sonnet 5.5 chat; the plan below was cut by the same chat, not by Top tier, so judge the design cards before Sonnet builds from them).*

- **Status: started, NOT done. One slice built (claim J2A), the rest planned as cards below.** Job 2 is ten pieces and several are whole domains (multiplayer, voice, 7 games); they need engine words that do not exist yet. Nothing here is called finished.
- **Old repo read:** `Barcode162/3d` attached read-only (clone at `/home/user/barcode162/3d` in this session). The old bar is C++ (`bar/*.cpp`, 7.7k lines for darts, piano, jukebox, avatar, bartender, hud), plus `bar/projects/{chess,mania,racing,platformer,meridian}`, `cloud/` (room.js, proto.js, games/) and `assets/{midi,music,piano}`. It has no `.loop` scripts to "bring over": everything must be rewritten in Looped Script or become engine words.
- **What changed, file by file:**
  - `tools/import_bar.py`: drops a wall that is listed twice (raised floors came in as both floor and solid). `projects/bar3d/bar.loop` regenerated: 293 to 282 blockers. Importer otherwise reproduces the old file exactly.
  - `work/claims/J2A.md`: claim (on `main`).
- **Hitbox finding (piece 2):** drew every blocker top-down (stools, tables, counter, walls). Stools are already separate small blockers from the old solids (seat slab 0.5 x 0.07 m), the counter is 66 thin pieces, walls and floor/ceiling slabs are right. I found no huge box except the 2.6 m wall footprints the old hall defines. I cannot walk it on a phone from here, so "feels right" is unchecked.
- **Proof run:** `cmake -B build -G Ninja ... && ./build/loop_tests && editor_source_test && recorder_test`: **262 tests, 0 failed; 45 editor checks, 0 failed; 36 recorder checks, 0 failed.** `./build/loop check projects/bar3d/bar.loop`: reads fine. The "4 library picture and 1 editor" failures from Job 1 do **not** appear on this tree (fresh build). Not run: picture tests, player walk-through (no phone here).
- **What Joshua should look at on his phone:** nothing new yet. The bar looks and plays as before, minus 11 doubled walls.

### Where each of the 10 stands, and the cards

| # | Piece | State | What it needs first |
|---|---|---|---|
| 1 | New light, dark oval, lamps, switch | lamps breathe/buzz, switch dims to 25% (done 2026-11-05); dark oval **open** | card **B1** (cinema render hunt) |
| 2 | Layout + hitboxes | layout framed (507/167/3 rooms); hitboxes reviewed, deduped | walk on phone |
| 3 | Scripts | none to move: old logic is C++ | cards G1-G7 rewrite it |
| 4 | Games (7) | not started | engine words E1-E3 |
| 5 | Clickables, seats | only "Use" near the switch | **E1 pick a thing** and **E2 sit** |
| 6 | Multiplayer, voice | not started; engine has no networking at all | **E4 network words**, then keep `cloud/` protocol |
| 7 | One avatar | engine `person` + 67-bone skeleton exist; not wired to bar | after E2 |
| 8 | Books, mirror, fire | fire sound only | **E5 mirror**, books card G8 |
| 9 | Platformer | not started | read `bar/projects/platformer` first |
| 10 | Music | not ready: no sample playback in the engine | **E6 sample playback** (else say so) |

**Engine cards (Strong or Top tier, one each, not parallel with J2A files):**
- **E1** Pick: a tap/aim on a placed thing sends `"use"` with `event` = that thing (name/id), so a seat, table or lamp can be named and answered.
- **E2** Sit: `player().sit(seat)` / `stand()` locks the player at a hip point and lowers the eye; seats imported from the old kinds (`parts.seats`: hip, facing, kind) as `seat` words.
- **E3** Small game surface: HUD text/buttons and a minigame loop that games G1-G7 draw into (needs a Top-tier design: it sets a seam).
- **E4** Network: a socket word speaking the old room protocol (`cloud/proto.js`), plus WebRTC voice with age groups apart. Safety rules in `CLAUDE.md` apply (13+, teens/adults apart).
- **E5** Mirror: a planar reflection of the room and avatar on `big_mirror`.
- **E6** Sample playback: play recorded sound (piano samples, jukebox m4a, CC-BY) mixed with the engine's own instruments.

**Game cards (after E1-E3, Workhorse each, one game per card):** G1 darts, G2 chess, G3 piano, G4 jukebox, G5 4K Mania, G6 racing (finish to playable), G7 bartender, G8 books (readable) and platformer. **B1:** find why cinema stills show a dark oval on the hall floor (suspects in `TRANSFER.md`).

- **Left undone / decisions for Joshua:** (a) the order: I recommend E1 + E2 first (sit and click make most of the bar feel alive), then E3 and the games one at a time, and E4 last because it touches safety; (b) music stays undone until E6; (c) the `LOG.md`/`PLAN.md` lines are for the deployer.
