# Handover: wave r1, stopped by the usage limit (2026-11-11, 06:20 UTC)

For the next chat that conducts. Read this, then `python3 tools/claims.py --remote`, then `docs/CONDUCTOR.md`.
Delete this file when wave r1 is shipped.

## The user's standing word (2026-11-11)
The reins are handed over: finish the loose ends and get much more in, with Sonnet helpers, and come back only
when it is ready for review, then tweaks, then a **grand release**. Nothing public changes before the user says
"release": not joshuavelasco.com's front page, not the live bar, not the public player. Everything new goes to
the hidden link only (joshuavelasco.com/bar/loop3d/, the hub's page at `hub/` under it; `docs/HUB.md` is the
update card). The user works from an iPhone and is out of usage until the limit resets: do not ask questions
that can wait for the review.

Two things are NOT to be done without the user (say so at the review, with a written plan):
1. Moving projects into `Barcode162/3D` (the user wants: LOOP3D the engine, 3D the projects and their storage).
   That repo keeps the live bar running; plan the split, do not start it.
2. Anything public (above).

## Shipped since (2026-11-11, by the chat that did SH1)
LOFT3, L-bed_platform, L-window_seat and **SH1** (the shader, whole: `docs/PIPELINE.md`, phase 8) are on main.
**N6 is smaller now:** SH1's shader lights any panel as its rectangle (light and shine), its sides by the film's
rule. What is left of N6: the test scene `strip_floor` with one long piece, its picture, and letting `strip` use
longer pieces. Its claim says `waiting`; set it `working` to take it.
The ten unfinished library entries are untouched on their branches.

## Where wave r1 stands
Every helper was cut off by the usage limit at about 06:10 UTC. Their work is saved on their branches.

| Job | Branch | State |
|---|---|---|
| LOFT3 (goal 3: deck, stair, rail, balustrade; the view moved to the glass looking back) | `work/LOFT3` | **done**, picture looked at by the lead, good. Ship it. |
| L-bed_platform | `work/L-bed_platform` | **done**, passed `library check`. Ship it (its claim was cleared from the board by a deploy that never reached `main`: check `work/claims/` and the branch). |
| L-window_seat | `work/L-window_seat` | **done**, passed `library check`. Ship it. |
| N6 (the strip's shine) | `work/N6` | not started in code. The lead decided its question: **no new field on `Light`**; the shader works out a panel's long side from the way it faces, by the rule film and `strip` share; film unchanged; stay inside the Touch line; the test scene may use `piece: 1`. Give it to a strong model. |
| L-kitchen_run, L-kitchen_island, L-built_in_shelves, L-sofa_modular, L-armchair_leather, L-coffee_table_slab, L-dining_table_oak, L-dining_chair_wood, L-bar_stool, L-floor_lamp_arc | `work/L-<name>` | **unfinished**: a recipe is written and saved as "wip", but its pictures were never looked at and its proof never passed. Start an asset-smith on each from its branch; the descriptions are in this chat's orders, repeated in each recipe's card. |

A deploy of the three done jobs was cut off halfway; `main` was left exactly as it was (nothing half-merged).

## What the wave taught
- Twelve asset-smiths each rendering three pictures at once put the machine's load at 70: every picture crawled.
  Start **four to six** at a time, or have them render one picture, not three, for things with `seeds: no`.
- A job on `projects/loft/` does not need the 12-scene picture tests (the Loft is not among them until N4).
- The usage limit stops every helper at once. Have helpers commit and push their recipe **before** rendering.

## Next, in order
1. Ship LOFT3, L-bed_platform, L-window_seat (the deployer).
2. Finish the ten unfinished library entries and N6; ship.
3. LOFT4 (the stair lights itself: needs LOFT3, N2, L-step_strips, all there), then N4 and M60 (after LOFT4, and
   M60 after N6: the same shader file), then LOFT5, 6, 7, 8, 9 in order (`docs/WORK_ORDERS.md`, 13.5).
4. More of the Loft's library (`docs/LIBRARY_PLAN.md`, section 4), in batches.
5. The hub's open lines (`docs/HUB.md`): the Loft playable from the page, the "what's new" list from the card.
6. The review for the user: what is done, the two-repo plan, then the release on their word.
