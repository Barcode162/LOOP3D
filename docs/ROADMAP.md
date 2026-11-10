# The roadmap: from a small engine to a giant

Written 2026-11-10, on the user's word: *"be critical and create a full roadmap to get it to that level … a real
library, genuine terrain, real people and skeletons, UIs that make themselves from the mechanics, the best assets
one at a time, real moves, real camera techniques, presets for any game and any film."* The user sent 16 reference
pictures: six loft interiors, five great landscapes, and five photos of their own desert hill.

This page is the long view. `PLAN.md` stays the order of work; it points here. The other pages of the set:

- `docs/WORK_ORDERS.md`: how work is cut into orders a smaller agent can finish, and the orders themselves.
- `docs/CRAFT.md`: the reference to keep forever: what makes those pictures work, and the rules of light,
  surfaces, set dressing, land, water, people, movement, camera, editing, sound and screens.
- `docs/MECHANICS_UI.md`: the design of mechanics that draw their own screens, and the game and film sheets.
- `docs/LIBRARY_PLAN.md`: the library to grow: every kind, the quality bar, and the first 400 entries in order.
- `.claude/agents/`: the four hands (builder, asset-smith, judge, checker) that carry out the orders.

---

## 1. The honest review

### What is truly good
The engine is five days old and about 37,000 lines, and its foundations are better than most small engines ever
get. These are real, and rare:

- **It measures itself.** A frame graph with timings, a work budget counted without a phone, a recording page
  on the real iPhone, picture tests, a sameness test, and a score against its own cinema renderer. Most hobby
  engines have none of these. This is the single most valuable thing here: it is what lets many small agents
  work without breaking each other.
- **A cinema renderer as ground truth.** The path tracer (now bidirectional) means "what should this look like"
  always has an answer.
- **One clock, deterministic.** Films scrub, tests are exact, multiplayer can stay in sync.
- **Clusters (Nanite's idea) working in WebGPU**, with occlusion culling, a light cache, kept shadows, upscaling.
- **Text is the source.** A language of 106 words, recipes cooked once, a library format with licences enforced.
- **Honest notes.** The plan records what was tried and dropped, with numbers.

### What is not there (measured against the 16 pictures)
Marks out of 10, where 10 is "could stand beside the reference":

| Area | Mark | The plain truth |
|---|---|---|
| Drawing machinery | 6 | Strong, but the phone still struggles: the bar holds only at the smallest drawing size, forests cost 6-8 ms just to find what's seen. |
| Surfaces (materials) | 2 | 59 named materials made of one colour and a flat pattern. No worn edges, no dust, no weave, no bumps you can feel. Model pictures are 256 pixels. This is why everything looks like plastic. |
| Things to put in a world | 1 | **The library has 6 entries.** One reference loft holds 300 to 500 different objects. The bar's 167 models are borrowed files, not recipes. |
| Ways to make a shape | 2 | Box, sphere, cylinder, cone, capsule, ring. Nothing that can make a sofa, a vase, a railing, a window frame or a leaf from words. |
| Land | 2 | The grown island has the right science (plates, rivers, rain shadow) but the picture is smooth grey cones on a green sheet with one tree. No ridges, cliffs, scree, strata, ground cover, far haze layers. |
| Water | 0 | A flat sheet. No river, no fall, no depth colour, no foam, no ripples. |
| Sky and air | 3 | A physical sky and sunlit fog. No clouds, no weather, no dusk city, no mist lying in valleys. |
| People | 1 | Jointed mannequins: about 17 bones, capsule limbs, no hands, no face, no hair, no clothes, no skin that bends. |
| Movement | 2 | One procedural walk, sit and look. No clips, no layers, no hands that grip, no falls. |
| Screens (UI) | 1 | Dialogue lines and choices in the web page. No bars, no bag, no map, no menus made by the game. |
| Game machinery | 1 | No entities with behaviours, no saving, no enemies or helpers that think, no paths round obstacles, no items. |
| Camera and film | 3 | Real lens values and two moves (shot, orbit). No rigs, no follow camera, no timeline, no cutting rules. |
| Sound | 3 | A good synth and placed sound. No sound files, no voices, no mixer. |
| Editor and tools | 4 | A phone editor that writes the script; no pack, no save to the repo, no asset browser. |

**As a foundation: 6 out of 10. As a picture next to the references: between 1 and 2.**

### The four hard truths
1. **The engine has outrun its content.** About three fifths of the code draws pictures, and there is almost
   nothing to draw. From here on, a new light technique moves the picture a little; surfaces, shapes and a full
   library move it a lot. The live picture's average distance from the engine's own cinema picture is 18 (10 is
   "plainly different"), but the bigger gap is that the cinema picture itself is of a bare room.
2. **The references are not real-time pictures.** The lofts are offline or AI-made images, the landscapes are
   photographs, one is a painting. No phone draws those at 60 frames a second, in any engine. So the target is
   two targets: **cinema** (`loop render`, minutes a picture) should reach them outright; **live** on the iPhone
   should be a convincing echo of the cinema picture, smooth first. Both are fed by the same recipes.
3. **"Done" has meant "the bare minimum works on the test machine".** That was the agreed rule and it was the
   right way to lay foundations, but several phone confirmations are still open in `PLAN.md`, and each minimum is
   a first rung, not a finished thing. This roadmap names the rungs.
4. **The code is not yet shaped for many hands.** `engine/gpu/renderer.cpp` (3,639 lines), `engine/gpu/shaders.h`
   (2,319) and `engine/script/scene_api.cpp` (1,936) are single files that every feature touches. Two agents
   working at once would collide in them. They must be split before work fans out (milestone 0).

### What to stop, for now
- New lighting techniques, until a scene exists that needs them (the judge's rubric will say when).
- Re-dressing Ardane's 24 scenes: they are built from boxes and will be rebuilt quickly once the library exists.
- Any work order that changes more than one part of `engine/modules.json` at a time.

---

## 2. The destination

**Any game, any film, from words, on a phone.** A person says what they want; the engine has the shapes, the
surfaces, the land, the people, the moves, the cameras, the sounds and the screens to make it; an AI fills in a
one-page sheet; the result plays at 60 frames a second on an iPhone and renders as cinema.

Three scenes prove it, taken straight from the pictures. Each is a ladder; every rung is one thing to see on the
phone.

### Star 1: Your Hill (the five desert photos)
A rocky slope of dark volcanic stones and pale grit, yellow wildflowers, cholla glowing white against the sun,
yuccas and Joshua trees, a wide valley floor dotted with scrub and small houses, blue mountains in layers, a
deep clear sky. **It comes first because it is the most reachable and it is yours:** a clear sky (already here),
one hard sun, sparse plants, no water, no people. And you can stand on the real one and say "no, the rocks are
darker than that."

### Star 2: The Loft (the six interiors)
Two floors under a high ceiling, a stair with a lit rail, a mezzanine bed, a kitchen under it, a deep sofa on a
shaggy rug, shelves full of small things, warm strips of light in every recess, a wall of glass with a city at
night or a sunset behind it, a cat. It proves surfaces, the library, interior light, glass, and people at rest.

### Star 3: The Valley (the five landscapes)
A turquoise river over pale stones, towers of rock in morning mist with shafts of sun, a plateau where a river
falls in a hundred threads, stone terraces on a ridge above the clouds. It proves land, water, air, growth and
the far view. Its last rung is the painted town on the falls: the same machinery turned to fantasy.

**Then two more that prove the engine is for making, not only for looking:**

- **Star 4: A game from a sentence.** "A lantern-lighting adventure on a desert hill at dusk" becomes a game
  sheet, then a project with bars, a bag, goals and menus nobody drew by hand.
- **Star 5: A short film.** Two people, one room, ninety seconds: shots that cut by the rules, faces that
  speak, music that follows.

---

## 3. Ten decisions that everything hangs on

Decided once, here, so that small agents never have to decide them. (They continue "the moves ahead" in
`PLAN.md`; none replaces one.)

1. **Shapes are made by words, not files.** A small set of shape words (lathe, extrude, sweep, bevel, carve,
   soften, bend, array, scatter) makes every library model from a text recipe. Imported `.glb` files remain
   possible but are never the plan. This is the unlock for an AI-grown library: a text model reads, diffs,
   varies by a seed, and has no licence problem.
2. **Surfaces are recipes in layers:** a base, variation, wear on edges, dirt in creases, a height. The cook
   bakes them to pictures (colour, bumps, roughness) at a set number of pixels per metre. The same recipe feeds
   cinema and live.
3. **Every thing says what it offers** (sit, hold, open, stand on, light, pick up) as named sockets in its
   recipe. People, the editor, the UI and the AI all read the same sockets.
4. **A world is a recipe of rules, not a painted map:** rock hardness, climate, a list of what grows where.
   Presets (desert basin, alpine river, tower karst, plateau falls, cloud ridge, coast, floating isles) are
   settings of one generator.
5. **One body, measured:** one standard skeleton of about 65 bones, a body grown from numbers (height, build,
   age), drawn "stylised real": the look of a good animated film, never a failed photograph. Faces and hands
   are the test.
6. **Movement comes from recorded human motion with a free licence,** turned into text clips on the standard
   skeleton, then layered, blended and corrected by reaching (IK) and by physics. Procedure fills the gaps;
   it does not replace the recordings.
7. **Mechanics declare, screens derive.** A game never draws a health bar. It declares a meter with a role
   ("vital"); the engine lays out, draws, animates and binds input for every role, for a phone thumb first.
   (`docs/MECHANICS_UI.md`.)
8. **A game is a sheet.** One page of choices (view, body, verbs, rules, loop, world, mood) that names a kit;
   the AI fills the sheet, the kit makes the project. A film is a sheet too (logline, beats, shot list).
9. **The camera is a rig with a named move, and a cut obeys rules the engine checks** (the 180-degree line,
   the 30-degree rule, headroom). Shots are composed for an upright phone first and reframed for wide.
10. **Two pictures, one source, one judge.** Every star has a rubric of ten plain questions
    (`docs/CRAFT.md`, section 12). The judge agent marks a render against it. The cinema score and the phone's
    milliseconds stay as they are. A step is good when: the rubric mark rose, the phone is no slower, nothing
    else changed.

---

## 4. The tracks

Twelve tracks. Each lists where it is, its rungs in order, and what "finished" means. The first orders of each
are written out in `docs/WORK_ORDERS.md` under the same track code.

### S · Shape words (the unlock)
*Now:* six primitive solids, `rock`, `tree`, `column`, `log`, plants.
1. `lathe` (a profile spun round: vases, bottles, lamp bases, columns, bowls, glasses).
2. `extrude` (an outline pushed up, with a bevel: table tops, signs, window frames, mouldings).
3. `sweep` (a section along a path: rails, pipes, cables, picture frames, arches, cove strips).
4. `round:` and `bevel:` on every edge that needs it; nothing in the real world has a knife edge.
5. `carve` (cut one shape from another: doorways, sinks, shelves' niches) and `join`.
6. `soften` (inflate between seams: cushions, mattresses, pillows, upholstery) and `drape` (cloth dropped
   once and kept: throws, curtains, tablecloths).
7. `bend`, `twist`, `taper`, `displace` (by noise: bark, stone, dents).
8. `array`, `along`, `mirror`, `scatter on:` (books on a shelf, balusters on a stair, bottles on a rack).
9. `part` and `socket` (named parts; what the thing offers), `variant` (a seed changes proportions and colours
   within the card's ranges).
10. `leaf card` and `strand` (plants, hair, fringes, shag rugs, grass: one system).
11. Automatic unwrapping (so surface recipes land with the right grain direction) and a triangle budget per
    size class, checked by `loop library check`.

*Finished when:* a chesterfield sofa, a crystal chandelier, a spiral stair and a Joshua tree are each under 80
lines of recipe and pass the judge at two metres.

### M · Surface recipes
*Now:* one colour, one of nine flat patterns, glass, glow, sheen, subsurface in cinema.
1. Layers: base, variation (large and small), edge wear, crease dirt, height. Curvature and hollows measured
   at cook time so wear lands on real edges.
2. Baked by the cook to colour, bump and roughness pictures; pixels per metre chosen by size class; mip chain.
3. Triplanar projection for rock, land and anything without a sensible unwrap.
4. Detail layer: a small repeating bump under two metres (weave, grain, pores, grit).
5. Fifty finished surfaces to replace the 59 flat ones, each with a card and a test ball render
   (oak with grain and pores, brushed steel, worn leather, boucle, velvet with a nap, concrete with form marks,
   brick with soft mortar, marble with veins that run through, desert varnish on basalt, decomposed granite).
6. Decals: stains, scuffs, puddles, labels, cracks, rugs' patterns.
7. Glass that is right: thin panes that reflect the room at night and show the world by day, with dirt.
8. Fabric and fuzz: sheen and a short fur shell for rugs, throws, moss.
9. Skin, eyes and hair shading (with track P).
10. The phone's own compressed pictures (ASTC), made by a build tool at pack time.

*Finished when:* the materials room scores above 8 of 10 on the rubric's "surfaces" question at half a metre.

### L · The library
*Now:* 6 entries; the card, the licence rule and `use` all work.
1. Kinds settled: surfaces, models, pieces (building parts), rooms, sets (a dressed corner), plants, rocks,
   lands, skies, light rigs, people, clothes, clips, moves, cameras, sounds, songs, screens (themes), kits.
2. The card grows: tags, size class, sockets, a seed range, a thumbnail (rendered by the cook), a triangle
   count, the judge's mark.
3. `loop library check` enforces the quality bar (`docs/LIBRARY_PLAN.md`, section 2).
4. A catalogue page on the phone: browse, search, turn a model round, place from the editor.
5. The factory: one entry per order, the asset-smith makes it, the judge marks it, a lead approves batches.
6. Growth targets: 100 (Your Hill dresses itself), 400 (The Loft), 1,000 (any room, any biome), then by need.
7. Sets and dressing rules: "a shelf, lived in" places 40 small things by rules, never by hand.
8. The bar's 167 borrowed models re-made as recipes, so the big files leave git.

### W · Land
*Now:* an 8 km island grown by plates, rivers, slopes at rest and rain shadow; streamed in chunks; smooth.
1. Ridges and gullies: the fine carving guided by where water flows, down to one metre.
2. Rock hardness in layers: hard caps make cliffs, mesas and waterfalls by themselves; soft beds make slopes.
3. Loose rock: scree under cliffs, boulders where they would roll to, fans where gullies open (your hill).
4. Ground surfaces blended by slope, height, wetness and flow, with stones that rise through the blend.
5. Ground cover near you: pebbles, twigs, tufts, flowers, as clusters, thinning with distance into the surface.
6. Growth by rules: each plant's needs (water, sun, slope, cold) and its elbow room; old and young together.
7. The far view: land to the horizon in coarse rings, mountains in haze layers that go blue with distance.
8. Presets: desert basin, alpine river valley, tower karst, plateau falls, cloud ridge with terraces, coast.
9. Roads, paths, walls and ruins laid by rules along the land (terraces follow level lines; paths avoid steep).
10. Caves, overhangs and arches (land that is not a height map), where a preset needs them.
11. Settlements: house plots along a road, the valley floor in your photos.

### A · Water and air
*Now:* a flat sheet; a physical sky; fog lit by the sun.
1. Still water right: depth colour (clear, green, turquoise, tea), the bed seen through, sky reflected.
2. Rivers that flow along the carved network: ripples that follow the current, slow pools and fast riffles.
3. White water where it is steep; wet banks; stones breaking the surface.
4. Waterfalls: sheets, threads, mist that rises and drifts, a rainbow when the sun is behind you.
5. Light dancing on the bed (caustics) and shafts under the surface.
6. Sea and lake waves by one formula that floating things use too; shore foam.
7. Wading, swimming, splashes, boats (with physics).
8. Clouds: a layer lit from below at sunset; then tall clouds; drawn slowly over several frames on the phone.
9. Mist that lies in valleys and pours over ridges; haze by distance and height.
10. Time of day and weather as one dial: dawn, noon, golden hour, dusk, night, overcast, rain, storm, snow.
11. A night city behind glass: towers, lit windows, haze, in layers that shift as you move.
12. Stars, moon, Milky Way (and sharp stars: an open gap).

### P · People and creatures
*Now:* a mannequin on about 17 bones.
1. The standard skeleton: about 65 bones, names matching common rigs, human joint limits.
2. A body grown from numbers, with skin that bends smoothly at elbows, shoulders, knees.
3. Hands with fingers that close round what they hold.
4. A head: a face with thirty sliders, eyes that focus, blink and dart, teeth, a tongue.
5. Expressions (about 20) and mouth shapes for speech (about 12), driven by the line being said.
6. Hair: cards live, strands in cinema; ten styles.
7. Clothes: layers grown from the body (shirt, trousers, jacket, dress, shoes), loose parts simulated.
8. Variety from a seed: age, build, skin, hair, outfit; a crowd where no two match.
9. Four-legged bodies on the same system: cat, dog, horse, deer; then birds and fish.
10. Close-up quality in cinema: skin that lets light in, eyes that are wet.

### V · Moves
*Now:* a procedural walk, run, sit, look; two-bone and chain reaching.
1. Clips as text on the standard skeleton; a reader for free-licence recorded motion (BVH) that fits any body.
2. The first 60 moves cleaned and looped (stand, walk, jog, run, turn, start, stop, sit, rise, crouch, jump,
   land, climb a step, pick up, put down, push, pull, open a door, wave, point, nod, lean, lie down).
3. Layers and masks: wave while walking; breathe under everything.
4. Choosing the next pose by matching (motion matching): turns and stops that never slide.
5. Feet that stay planted, hands that reach sockets, eyes then head then chest turning to look.
6. Using things: sit on that chair, lean on that rail, open that door, carry that box, from the thing's sockets.
7. The living body (the rebuild's steps 7 and 9): stumbles, falls, catches itself, gets up.
8. Acting: a mood changes posture, pace and gestures; idles that are never the same twice.
9. Speech: the mouth follows the line; gestures land on stressed words.
10. Styles: the same walk as tired, proud, sneaking, old.
11. Animals' gaits; flocks, shoals and herds.
12. Fights, sports and dance, as sets of moves, when a kit needs them.

### U · Mechanics and screens
*Now:* lines, choices, sticks. *Design:* `docs/MECHANICS_UI.md`.
1. Screens drawn by the engine itself (sharp text at any size, rounded panels, icons) so they are in films and
   pictures and the same everywhere.
2. The roles and the layout solver for an upright phone, a wide phone and a computer.
3. Mechanics as words: `meter`, `counter`, `timer`, `goal`, `bag`, `action`, `prompt`, `map`, `message`.
4. One input list: an action is a thumb button, a key and a pad button at once; a rebinding screen for free.
5. Screens for free: pause, settings, bag, goals, map, save slots, shop, credits (from the library's cards).
6. Themes: ten skins (clean, ember, neon, parchment, toy, terminal, glass, comic, noir, nature).
7. Feel: numbers that count, bars that flash when hit, prompts that breathe; all from the role, none by hand.
8. Reading for everyone: text size, contrast, colour-blind safe, hold instead of mash, captions.
9. The radial menu from the player's menu plan as the phone's main way in.
10. The editor's Screens space: move a role's home, choose a theme, never draw a rectangle.

### K · Game machinery and kits
*Now:* scenes, events, timers, tasks, story memory.
1. Things with behaviours: `kind`, properties, events (`when touched`, `when used`, `when near`), prefabs.
2. Saving and loading the whole world, by the one clock.
3. Paths round obstacles on any ground; crowds that avoid each other.
4. Minds for helpers and opponents: senses, a goal, a plan from a short list of behaviours.
5. Items, recipes, shops, levels of skill; conversations with memory.
6. Kits, each a playable template and its sheet: story walk, exploration, platformer, puzzle rooms,
   top-down adventure, racing, flying, survival and craft, farm and build, tycoon, stealth, rhythm, cards and
   boards, sports, tower defence, role-playing.
7. Vehicles (the old engine's tyre model), flight, boats.
8. Multiplayer rooms on the one clock, safety first (13+, teens and adults apart).
9. `loop new --sheet`: an AI fills the sheet, the kit builds, `loop check` and a play-through test agree.
10. A robot player that finishes each kit's sample, so no kit ever breaks unnoticed.

### C · Camera and film
*Now:* a real lens, `shot`, `orbit`, looks, shake.
1. Rigs: tripod, dolly, crane, arm, handheld, drone, shoulder, follow; each with named moves.
2. Game cameras: follow with a soft zone, an arm that shortens at walls, look-ahead, framing two people.
3. Composing: thirds, headroom, lead room, a foreground, for upright and wide from one shot.
4. Focus as a tool: pull focus, shallow close-ups, deep landscapes.
5. The timeline: shots, people, lines, music and sound on tracks, scrubbed both ways (rebuild step 10).
6. Cutting rules checked by `loop check`: the line, the 30 degrees, matching eyelines, cut on movement.
7. A director: from a beat ("she tells him; he doesn't believe it") to a shot list by the rules of coverage.
8. Light rigs as library entries: three-point, window light, golden hour, moon and practicals, candle.
9. Grading and film looks per shot; titles and credits.
10. Rendering a film on the phone overnight, or in the cloud session, to a video file.

### X · Sound
*Now:* a synth with instruments, placed sounds, a room echo.
1. Sound from files (free-licence recordings) beside the synth; the pack shrinks them.
2. A mixer with groups, ducking (music dips under speech), on its own thread.
3. Rooms that sound like their size and surfaces, from the light cache's probes.
4. Footsteps, knocks and scrapes from the surfaces touching, at any speed.
5. Music that follows the game: layers that enter with tension, endings on the bar.
6. Voices: recorded lines per character when the user supplies them; a from-scratch placeholder voice otherwise.
7. Weather, water, fire, crowds and wildlife as living beds made of many small sounds.

### E · Engine, picture and tools (the floor under the rest)
*Now:* see the review.
1. **Split the three great files** into parts with one job each (milestone 0).
2. The pack (Phase B), the project file, memory that fits, pictures compressed.
3. Interior light for the Loft: glowing strips and panels as true lights in the cache, window light, reflections
   in glass and polished floors on Phone, glare from bright bulbs.
4. Depth of field, motion blur and soft particles live.
5. Wind in everything that grows or hangs.
6. Particles and sprites (dust in sunbeams, sparks, mist, rain, leaves).
7. A debugger for scripts; warnings; a profiler page.
8. The editor's next hands; saving to the repo from the phone; the asset browser.
9. Check on GitHub runs each agent's order in a fresh checkout; a nightly picture run.
10. Every helper script folded into `loop`.

---

## 5. The milestones (what you will see, in order)

Each is a set of rungs across tracks. The number is roughly how many work orders it holds, to give its size
without pretending to know dates.

| # | Name | You see on the phone | Tracks | Orders |
|---|---|---|---|---|
| 0 | **Many hands** | Nothing new: the same 12 pictures, the same speed. The great files are split, the four agents exist, the rubric judges. | E | ~25 |
| 1 | **Your Hill** | You stand on a rocky desert slope at mid-morning: dark stones, cholla lit from behind, yuccas, the valley with its little houses, blue mountains. | S, M, W, L(100), A(haze) | ~90 |
| 2 | **Surfaces you want to touch** | The materials room, and every old scene, stops looking like plastic. | M, S | ~50 |
| 3 | **The Loft, empty and lit** | The two-floor room with stairs, glass wall, night city, warm strips of light. | S, E(3), A(11) | ~60 |
| 4 | **The Loft, lived in** | Sofa, rug, shelves of things, kitchen, bed, plants, the chandelier. | L(400), M | ~320 |
| 5 | **Screens that make themselves** | The Lantern Isle gets a goal counter, a bag and a pause menu from four new lines of script. | U | ~55 |
| 6 | **The Valley** | The turquoise river, the towers in mist, the falls at sunset. | W, A | ~110 |
| 7 | **A person worth a close-up** | Someone sits on the Loft's sofa, looks up, smiles. A cat on the rug. | P, V | ~140 |
| 8 | **A game from a sentence** | You type one line; a small game with its own screens plays. | K, U | ~120 |
| 9 | **A short film** | Ninety seconds, two people, cut by the rules, rendered as cinema. | C, V, X | ~90 |
| 10 | **The giant** | A thousand library entries, sixteen kits, seven land presets, the bar rebuilt and switched. | all | open |

Milestone 1 is the recommendation for what comes next after 0: it is the cheapest proof, it exercises five
tracks at their easiest, and its reference is under the user's own feet.

**The pace rule still holds.** The user sees one tiny goal at a time, with the one idea it teaches. What changes
is behind the curtain: orders are cut small enough that several hands can carry them between the goals the user
watches, and a rung is shown only when it is checked.

---

## 6. How the work is run

- **The lead** (the strongest model available, in the user's chat) owns this roadmap, cuts orders, decides
  anything not decided here, reviews batches, talks to the user, and keeps `PLAN.md` and `LOG.md`.
- **The four hands** (`.claude/agents/`): the **builder** carries one engine order; the **asset-smith** makes
  one library entry; the **judge** marks a render against a rubric and says the one thing to fix next; the
  **checker** runs the quick checks and reports plainly.
- **An order** is small, names the files it may touch, and ends in a command that passes or fails
  (`docs/WORK_ORDERS.md`). An agent that cannot finish stops and says why; it never widens its order.
- **Nothing reaches `main` unchecked:** quick checks, the order's own test, the picture tests when the picture
  could change, the judge when the picture should change.
- **Risks, watched:** the phone's memory (a thousand cooked models), the first download's size, heat on long
  play, Safari's WebGPU limits, licence of every recorded motion and sound, and the uncanny valley (decision 5).

When a stronger model is available again, spend it on what small hands cannot do: the splits of milestone 0,
the shape words' design (S1 to S9), the layout solver (U2), the motion matcher (V4), the land's carving (W1,
W2), the director (C7), and reviews of the judge's marks.
