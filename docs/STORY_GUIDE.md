# Turning a story into Loop3D

This guide is for whoever turns a written story into Loop3D: usually an AI working from the author's story. Read [`LANGUAGE.md`](LANGUAGE.md) first for the words; this page is about how to use them well.

## How a story is laid out

- **One `.loop` file is one scene:** one place and what happens there.
- **A story is a folder of scenes.** The player starts with one, and each scene says where to go next with `go_to "next scene"`.
- **Scene names are file names:** `go_to "the_woods"` loads `scenes/the_woods.loop`.
- **Each scene starts with:**
  - `title "…"`
  - `mode "movie"` (it plays its shots by itself) or `mode "game"` (the player walks; the left thumb walks and the right thumb looks)

```
stories/harthwake/
  01 the village.loop
  02 the woods.loop
  03 the river.loop
  ...
```

## Building one scene, in this order

1. **The place.** Ground or land, buildings, furniture, nature.
   - Use **named materials** ("oak", "brick", "brass", "wool"…) before making new ones; they're tuned to look real.
   - Real sizes in real units: a door is 2.1 m tall, a table top 75 cm high, a chair seat 45 cm.
2. **The light.** Decide the time of day first. It shapes everything.
   - Outdoors: `sun elevation: …, toward: …` with `sky "physical"`. A low sun (3–12°) gives long golden light; 30–60° is midday.
   - Night: a weak sun (lux 0.1–1, kelvin 4000) as the moon, plus `sky "physical", stars: 0.02`.
   - Indoors: lamps with real brightness:

     | Light | Lumens | Kelvin |
     |---|---|---|
     | candle | 12–15 | 1850 |
     | oil lamp | 50–100 | 2000 |
     | household bulb | 800 | 2700 |
     | fire | 1000–2000 | 1900 |

   - Light comes from things: put a `light` at every flame, bulb and window, and give each a glowing shape ("flame", "ember", "lamp shade").
   - A little `fog` makes light beams visible.
3. **The people.** `person(at, height, shirt, trousers)`, then give them life:
   - `walk_to` somewhere, `look_at` what they'd look at, `sitting = yes` on a seat.
   - Footsteps sound by themselves.
4. **The sound.** Every place has a bed of sound: `sound "wind"`, `"birds"`, `"crickets"`, `"stream"`, `"rain"`, `"night"`, `"fire"`, `"hum"`.
   - There are also beds for people and machines: `"crowd"`, `"applause"`, `"strings"` (a slow quartet), `"synth"`, `"chains"`, `"drones"` and `"heartbeat"`.
   - Put endless sounds where they come from (`at:` the fire, the river).
   - `room size: …` sets how big the space sounds (outdoors 0, a small room 0.3, a hall 0.7).
5. **The story beats:** a `task` that runs the scene in order.

   ```
   task
     wait 2 s
     mira.look_at = vec(0, 1.6, 4)
     wait line("Mira", "The lamps have all gone out.")   # `line` gives back how long it shows
     ask "What do you say?", ["I'll come.", "Not tonight."]

   when "choice"                     # `event` is the option picked, 0 = the first
     if event == 0
       wait line("Mira", "Then stay close.")
       go_to "the woods"
     else
       wait line("Mira", "Then I'll go alone.")
   ```

   - Within one scene, plain names at the top level remember things (`let kindness = 0`; later `kindness += 1`).
   - Across scenes, use the story's memory, which survives `go_to`:
     - `remember "took the coins", yes` stores a value
     - `recall("took the coins", otherwise: no)` reads it back
     - `forget_story()` clears it (the first scene calls it, so a new playthrough starts fresh)

6. **The camera and the look.**
   - A `look` that fits the mood: "golden" for warmth, "noir" for mystery, "horror" for dread, "dream" for memories, "natural" otherwise. Add `bars: 2.39` for a film frame.
   - For movie scenes, `shot`s in order:
     - start wide (24–35 mm) to show the place
     - go close (50–85 mm, `f_stop: 1.8–2.8`) for faces and small things
     - use `ease: "smooth"` (the default) so moves start and stop gently
     - a little `shake: 0.2` for tension
     - `fade_in` on the first shot, `fade_out` on the last
     - make the shots together last about as long as the scene's talking. A story scene holds its last shot until the script moves on, and `loop play` prints both lengths so you can match them.
7. **The trailer still.** `render file: "<scene>.png", quality: "cinema", at: 5 s` gives the Trailer view its picture. Pick the moment that best sells the scene.

## The player's part (game scenes)

- `player()` is where the player stands; `near(vec(…), within: 2 m)` says if they're that close.
- Pressing **Use** (the E key, or the Use button on a phone) sends the event `"use"`:

  ```
  let used_at = -1
  when "use"
    used_at = time()
  ```

- `move_player(at: vec(…), look_at: vec(…))` carries the player somewhere new: waking up somewhere else, a door into another place.
- People can be hidden (`.visible = no`) and stood above or below the ground (`lift: 0.6` or `.lift = 0.6` for a dais, a tower top or the air).
- Scenes can share their pieces: `use "common"` at the top of a scene brings in `common.loop` from the same folder (once), with its materials, people and helper functions.
  - [`stories/echoes_of_ardane/ardane_common.loop`](../stories/echoes_of_ardane/ardane_common.loop) is a full example. Its `go_and_use(place, hint)` waits for the player to walk somewhere and press Use, and its `choose(question, options)` asks and returns the answer.

## Rules that keep it believable

- Real scale and real light units, always. Don't fake brightness with glow: add a light.
- Keep the 0.5 m around walking routes clear (people walk straight to where they're sent).
- Moving things ease. Use `approach(...)` and `ease(...)` for anything a script moves every frame, and `every 1 / 30 s` for flicker and sway.
- Vary repeated things: a random `turn:` and `scale:` on rocks and trees, `flicker(time(), speed, seed)` with a different seed per lamp.
- One idea per scene. If a place needs two moods (calm, then a storm), use two shots or two scenes.

## Checking the work

1. Run `loop check scene.loop` on every file. Fix every problem it names; the messages say the line and usually the fix.
2. Run `loop run scene.loop --seconds 30`. It plays the scene without pictures and reports problems that only show while running.
3. Run `loop play first_scene.loop --choices 0,1,0` to play the whole story with no picture. It presses Use, answers the questions (with the options listed, then the first), follows every `go_to`, prints every line, and reports any problem in any scene. Try every choice at least once.
4. Run `loop render scene.loop --quality draft`, then look at the picture. Too dark or bright? Wrong mood? Things floating or sunk into the floor?
5. Open it in the player (`?scene=<name>`) for the live version, in movie and game mode.

## What to ask the author

Ask only when the story doesn't say:

- the time of day, the weather and the season of each place
- the look of the main people: height, clothes and their colours
- whether a scene is watched (movie) or played (game)
- which moments deserve a still for the trailer
