# Mechanics that draw their own screens, and the two sheets

The design behind track U (screens) and track K (kits) of `docs/ROADMAP.md`. Decided 2026-11-10; nothing here is
built yet. Builders: follow this page; if it is silent or wrong, stop and tell the lead.

## 1. The idea

In most engines somebody draws a health bar, places it, wires it to a number, animates it, then does it all
again for a phone. Here **a game never draws a screen.** It declares what the player must know and what the
player can do. Each declaration has a **role**. The engine knows where each role lives on a phone held upright,
how it looks in the chosen theme, how it moves when its value changes, and which thumb, key and pad button
reaches it. Add a mechanic, and its screen is there. Remove it, and the screen closes up.

It is the same trick the engine already uses for settings: one schema makes the editor's panel, the docs and
the checks. This extends it to play.

## 2. The words (track U, rung 3)

```
let health = meter("health", max: 100, role: "vital", icon: "heart")
let coins  = counter("coins", role: "resource", icon: "coin")
let air    = meter("air", max: 30, role: "vital", shown: "when changing")
let clock  = timer("time left", seconds: 120, role: "pressure")

goal "Light the five lanterns", count: lit_count, of: 5
goal "Find the boat key", optional: yes

let bag = bag("bag", slots: 12)
bag.add("lantern oil", 3, icon: "flask")

action "jump", when: on_ground
action "light", when: near_lantern, label: "Light", main: yes
prompt "Open", at: door, within: 2           # an action tied to a place
message "A cold wind rises.", kind: "notice"
map marks: lanterns, player: yes
```

Each gives back a handle with plain fields (`health.value -= 10`, `coins.value += 1`, `clock.paused = yes`) and
sends events (`"health empty"`, `"time left over"`, `"jump"`).

## 3. The roles and where they live

| Role | What it is for | Upright phone | Looks like | Moves when |
|---|---|---|---|---|
| vital | what ends the game if it runs out | top left | bar or pips with an icon | flashes on loss, pulses under a quarter |
| resource | what you gather and spend | top right | icon and number | counts up, pops on gain |
| pressure | time, a chaser, a storm | top centre | ring or shrinking bar | reddens in the last tenth |
| goal | what to do now | under top centre, one line | text with a count | slides in, ticks, strikes through |
| action | a verb always at hand | bottom right arc, main one largest | round button, icon | dims when `when:` is false |
| prompt | a verb for this place | above the action arc, centre low | pill with a label | breathes; follows the thing at the edge of the screen |
| move | walking, steering | bottom left | the thumb stick | fades when idle |
| look | turning | right half, anywhere | nothing drawn | — |
| choice | pick one of a few | centre low, stacked | cards | one at a time, thumb-high |
| line | someone speaks | low third | name and text | fades in and out (as now) |
| message | the world tells you | upper third | toast | queues, never stacks more than two |
| bag | what you carry | a button by the menu; opens a sheet | grid of slots | new item wiggles the button |
| map | where you are | a button; or a small round one top right | the land from above with marks | — |
| menu | everything else | the round button (the player's menu plan) | radial | hold to open, slide, let go |
| meter over a thing | an enemy's health, a door's lock | on the thing, in the world | small bar | only when changed or looked at |

Rules of the solver (rung 2):
1. **Thumbs own the bottom third; eyes own the top third; the middle belongs to the world.** Nothing sits in
   the middle but a message, briefly.
2. Each zone has a capacity. Past it, items of the lowest priority collapse into one that opens (three vitals
   fit; a fourth becomes a ring).
3. A role with `shown: "when changing"` appears, holds two seconds, and goes. Full health hides by default.
4. Safe areas, the notch and the home bar are respected. A wide phone mirrors the upright layout's zones to the
   two sides; a computer puts actions on a bottom bar with their keys printed on them.
5. Left-handed play swaps move and actions. One-handed play stacks both on one side.
6. Every button is at least 44 points, 8 apart. Text is never below 13 points.
7. The world's own picture decides contrast: each panel samples what is behind it and picks light or dark.

## 4. Screens that come free (rung 5)

- **Pause:** resume, settings, goals, save, leave; the settings come from the schema that already exists.
- **Bag:** the grid, an item's card (its library card's first line), use, drop, combine if recipes exist.
- **Goals:** open and done, optional ones apart.
- **Map:** made from the land's own height map and the scene's marks.
- **Save slots:** a small picture of the moment, the place, the time played.
- **Shop and craft:** from a list of items with prices or recipes; never real money.
- **Controls:** every `action` with its thumb place, key and pad button; rebinding.
- **Credits:** made from the cards of every library entry the project used (credit and licence), so nothing
  is ever uncredited.
- **Title screen:** the project's cover page: title, a shot of the first scene, play, continue.

## 5. Themes (rung 6)

A theme is a small table: fonts (free licences only), a corner radius, line weight, six colours with their
dark-background twins, a motion table (how long, which ease), a sound per event (tap, gain, loss, done). Ten to
ship: clean, ember, neon, parchment, toy, terminal, glass, comic, noir, nature. A project names one and may
change single entries. A theme is a library entry (`library/screens/`).

## 6. Drawn by the engine (rung 1)

Screens are drawn in WebGPU by the engine: text from distance-field letters (sharp at any size, made from the
font file by the cook, from scratch), rounded panels and rings from one shader, icons from the 51 J Production
icons. Reasons: they appear in films and picture tests, look the same on every device, can sit in the world
(a meter over a thing), and cost one draw. The web page keeps only what a browser must do: typing text.
The whole of it is one more step in the frame graph ("screens"), timed like the rest; budget 0.5 ms on Phone.

## 7. The game sheet (track K)

A game is a point in a space of a few choices. The sheet is those choices on one page (a section of the
project's `.looped` cover page). A kit is a named sheet with a working sample.

```
sheet
  view:    "third person"      # first person | third person | top down | side on | fixed | on rails
  body:    "walker"            # walker | driver | flyer | swimmer | hand (taps and drags) | none
  verbs:   ["light", "carry", "talk"]
  aim:     "collect"           # reach | collect | survive | solve | build | beat | tell
  against: "the clock"         # nothing | the clock | the world | minds | other players
  shape:   "one open place"    # rooms in order | one open place | chapters | endless | rounds
  grows:   ["a bag", "new verbs"]
  world:   "desert basin, dusk"
  mood:    "quiet, a little eerie"
  length:  "ten minutes"
```

From a sheet the kit lays down: the camera rig for the view, the controller for the body, an `action` for each
verb, the `goal`, the `timer` or minds for "against", scenes for the shape, a land preset and time of day for
the world, a theme, a light rig and a music bed for the mood. The AI then writes only what is particular: the
places, the people, the lines, the surprises.

Sixteen kits to ship (track K, rung 6), each a sheet plus a ten-minute sample: story walk, exploration,
platformer, puzzle rooms, top-down adventure, racing, flying, survival and craft, farm and build, tycoon,
stealth, rhythm, cards and boards, sports, tower defence, role-playing. Combined sheets ("a racing game with a
story") are allowed wherever two kits' choices do not clash; `loop check` names the clash.

**Safety is part of every kit:** 13+ content only; anything with other players keeps teens and adults apart;
games of chance use earned credits only, never money.

## 8. The film sheet (track C)

```
film
  logline: "A girl returns a lantern to the hill where her grandfather kept it."
  length:  "90 seconds"
  place:   "desert basin, last light"
  people:  ["Mara, 12, careful", "the wind"]
  look:    "golden"
  beats:
    - "The hill, empty. Wind."               # establish
    - "Mara climbs, the lantern held close." # follow
    - "She finds the stone post."            # discover
    - "She lights it. Her face in its glow." # the turn: close
    - "From far off: one light on the hill." # release: wide
```

The director (track C, rung 7) turns each beat into shots by the rules in `docs/CRAFT.md` (sections 9 and 10):
a size for its job, a lens for its size, a move for its feeling, a length for its pace, and cuts that keep the
line. The result is ordinary `shot` lines in a scene, which a person or the editor can then change.

## 9. Build order (the orders are in `docs/WORK_ORDERS.md`, track U)

1. Letters and panels drawn by the engine; the "screens" step in the frame graph.
2. The solver on an upright phone with fake items; picture tests for 1, 3 and 7 items a zone.
3. `meter` and `counter`; then `goal`; then `action` and `prompt`; then `timer`, `message`; then `bag`; `map`.
4. The input list (thumb, key, pad) and the controls screen.
5. Pause, goals and bag sheets; save slots when saving exists (K2).
6. Themes; feel; reading for everyone.
7. The Lantern Isle moved onto it as the proof (milestone 5), with its old hand-made counters removed.
