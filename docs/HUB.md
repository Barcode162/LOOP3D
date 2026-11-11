# The hub: joshuavelasco.com

*The user's word, 2026-11-11:* joshuavelasco.com becomes the main page for J Production and Loop3D, redone, and
it is the main job for now. This repo stays the work in progress. Nothing goes public piece by piece: this page
is the **update card**, filled in little by little, and the hub is published once, when the card is full.

The live bar (joshuavelasco.com/bar, from `Barcode162/3D`) is not touched by any of this until the user says
"Switch" (`PLAN.md`, phase C).

## The update card: Hub, first publish

A line is ticked only when it has been checked, on the phone where it says so.

**The page itself** (`web/hub/`)
- [ ] The front page: what J Production is, what Loop3D is, one button to play. Fits an iPhone, loads fast.
      *Built 2026-11-11 (`web/hub/index.html`, no downloads but its three pictures); waits for the user's eye on the phone.*
- [x] Three pictures from the user (below), placed, with their credit line.
- [x] Every picture made by the engine is marked as such, and every concept picture as concept art.
- [ ] A "what's new" list on the page, written from this card.

**Things a visitor can use**
- [ ] The player, at the hub (today: barcode162.github.io/LOOP3D).
- [ ] The Loft at night, goal 2 or later, playable from the front page.
- [ ] The editor, linked from the hub.

**The pipe** (so publishing is one word)
- [x] The three IONOS secrets are in this repo's GitHub settings (only the user can).
      *Checked 2026-11-11 by the key check (`.github/workflows/hub-keys.yml`, looks only): logged in; `home/bar` is
      there, `home/bar/loop3d` not yet. The host and user are saved with a new line at their end: every workflow
      that uses them trims it, as the key check does.*
- [ ] The Cloudflare token works (needed for the later part, not the first publish). *2026-11-11: refused,
      "Invalid API Token"; the account ID has the right shape.*
- [ ] One workflow builds the hub and the player and uploads them; started by hand for the first publish.
- [ ] After the first publish: every green Check on `main` uploads by itself.
- [ ] The live page is opened and checked after an upload, by the workflow.

**Later, not in the first publish**
- The user's database holds the phone's timed runs and the job board (needs: what the server offers).
- The Workshop dashboard at the hub.
- The bar on Loop3D ("Switch").

## The pictures

Made by the user with Gemini, from prompts written in the chat of 2026-11-11. They are concept art, never shown
as the engine's own picture; each is replaced by a real engine picture when one is good enough.

| File (`web/hub/images/`) | Shape | What it is | Arrived |
|---|---|---|---|
| `hero_loft_night` | tall, 9:16 | The Loft at night, the top of the front page on a phone | 2026-11-11 |
| `recipe_to_world` | wide, 16:9 | A world half drawn as lines, half finished: "text is the source" | 2026-11-11 |
| `loop_mark` | square, 1:1 | A glowing loop, the mark beside the name | 2026-11-11 |

Credit line for each: "Concept art, made with Google Gemini for J Production." No text, logos, brands or real
people in any of them.
