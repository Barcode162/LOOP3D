# The tuning sheet

Every setting, by name, with its range, default, status and script word, is in [`SETTINGS.md`](SETTINGS.md) (made from the editor's and the engine's own lists). This sheet explains what they cost and how they trade.

Every setting that changes how Loop3D looks or how fast it runs: what it does, what it costs, what it buys, and how the settings trade against each other. The second half lists the performance tricks we have and the ones we're adding.

**The goal is smoothness you can feel, not just a high number.** A steady 60 fps (a new picture every 16.7 ms, every time) feels better than 90 fps that stutters. So we judge the engine in this order:

1. **Steady frame time.** No hitches, no freezes, no frame much longer than the others.
2. **Low delay.** The view answers the hand within a frame or two.
3. **High frame rate.** 60 everywhere; 120 where the screen can show it.
4. **Picture quality.** As much as fits in what's left.

How to read the tables:
- **Cost:** what the setting takes from the frame budget (GPU time unless it says CPU).
- **Gives:** what it adds to the picture.
- **Tune with:** the settings it trades against.

---

## 1. The quality levels

The player has three live levels, picked from the menu or with `?quality=0|1|2`.

| Setting | Phone | High | Ultra | What it does | Cost | Tune with |
|---|---|---|---|---|---|---|
| Pixel density cap | 1× | 1.5× | 1× | the most pixels per screen point we draw | grows with the square: 2× is 4 times the pixels of 1×; past 1.5× the eye barely sees more | automatic resolution (it scales this down) |
| Anti-aliasing (MSAA) | 4× | 4× | — | smooth edges | memory and fill rate, ×4 on the scene pass | pixel density: at 2×, MSAA matters less |
| Sun shadow map | 1024² | 2048² | — (traced) | sharpness of sun shadows | one depth pass of every object | shadow area |
| Sun shadow area | 25 m | 40 m | (traced) | how far around the camera sun shadows reach | wider area = blurrier shadow at the same map size | shadow map size |
| Sun shadow softness | 12 taps | 12 taps | (traced) | soft, filtered edges | 12 texture reads per pixel | — |
| Lamp shadows (ray-traced) | none | 2 lamps, 1 ray each | every lamp | real shadows from lamps, the brightest near the camera first; panels (windows, softboxes) never cast them live | **the most expensive live setting**: one ray per lamp per pixel costs about as much as all the other lighting (measured); 4 rays for soft edges cost 4 times that | `?lamps=N&rays=N`, resolution |
| Depth pre-pass | off | off (`?prepass=1`) | — | depth first, then light only the nearest surface | one extra geometry pass | measured no faster; needs both passes to give exactly the same depth, which Apple GPUs don't promise |
| Lamps lit per pixel | up to 256 | up to 256 | 1 random lamp per bounce | every lamp lights every pixel it reaches | small per lamp, but adds up past ~30 | clustered lights (planned) |
| Bounces | 1 (plus sky) | 1 (plus sky) | 5 | light bouncing off walls (Ultra is a path tracer) | Ultra: a full ray path per pixel per frame | denoiser, resolution |
| Denoiser | — | — | 4 passes (à-trous) | turns Ultra's noise into a clean picture | 4 blur passes | history length |
| Ultra history | — | — | grows; 8 frames when things move | how many frames are averaged into one | none (it's how clean Ultra gets) | motion: a longer history smears moving things |
| Starting resolution | 87% | 87% | 65% | where automatic resolution starts (it climbs when calm) | — | a first frame too heavy can't make the browser take the GPU away |
| Lowest automatic resolution | 50% | 50% | 35% | how far the player may drop to keep up | — | pixel density cap |

---

## 2. Automatic controls (the player tunes itself)

| Control | Now | What it does | Why |
|---|---|---|---|
| **Automatic resolution** | judged about once a second **of real time**: when frames are missing (below 88% of a steady 60, or of the screen's rate if lower) *and* the GPU's time per frame explains it, it jumps straight to the size that should fit; no finished frame for 2 s, the smallest size at once; calm 3 s and the next size should fit, a step up, never straight back to a size that was too slow (that limit lifts after 20 calm seconds, not on Ultra). Steps: 100, 87, 75, 65, 56, 50, 42, 35% | keeps the frame rate up by drawing fewer pixels | it reads real time and the GPU's own "done" times, because an overloaded GPU makes the browser stop asking for frames: counting skipped frames, or adding up frame times, then never sees the problem |
| **Free resolution changes** | the scene is drawn into the top-left part of full-size targets; the post passes read that part and stretch it to the screen | a change costs nothing: no new textures, no canvas resize | before, every change re-made the canvas and every target (hundreds of MB), and the old ones lingered until garbage collection |
| **Frame gate** | at most 2 frames waiting on the GPU | a slow GPU drops a frame instead of falling seconds behind | keeps delay low |
| **Watchdog** | no "done" from the GPU for 2 s → stop waiting, and ignore those frames' signals if they turn up | stops a permanent freeze | before, late signals were still counted, so the gate let more and more frames pile up on the GPU |
| **Lost GPU** | the browser took the GPU away → get a new one, send it the whole world, carry on; twice in a minute → one quality lower | no black screen for good | driver resets, memory running low, frames that run too long |
| **GPU memory** | old textures and buffers are destroyed at once (resize, new scene); Ultra's buffers exist only on Ultra and fit the GPU's limits (asked for at start) | memory stays flat through a whole story | the browser only frees them when it collects garbage |
| **Script clock** | 120 steps a second; catches up after a slow frame (up to 12 steps, never more than 8 ms of work), then drops the rest; the film camera drops the same time | scripts and people move smoothly at any frame rate, and the camera stays in step with the lines | before, at most 4 steps: below 30 fps the script ran slow while the camera ran on |
| **Change tracking** | separate counters for "things moved", "lamps changed" and "the world changed" | moving or flickering things cost almost nothing | before this fix, every frame rebuilt the whole scene on the GPU |
| **Lamp order** | re-sorted when the camera moves 0.5 m | shadow rays go to the lamps that matter | the shader gives its shadow rays to the first lamps in the list |

**Stats line** (bottom of the player): frames that reached the screen, CPU milliseconds per frame, resolution in use, number of things.
- Low fps and low CPU ms: the GPU is the limit. Lower the resolution, the lamp shadows or MSAA.
- High CPU ms: the script or the world is the limit. Use fewer people or physics bodies, or longer `every` intervals.

---

## 3. The film (post-processing)

Every scene sets these with `look …`. Most are nearly free (one full-screen pass does them all), except bloom and exposure.

| Setting | Range | What it does | Cost | Tune with |
|---|---|---|---|---|
| `look` name | natural, blockbuster, noir, golden, bleach, seventies, neon, dream, documentary, horror, arctic, western | a whole grade: tone curve, colour, grain, halation | free | the others below adjust it |
| `contrast` | 0.8–1.3 | how hard the darks and lights are pushed apart | free | `exposure`; high contrast hides noise in Ultra |
| `saturation` | 0–1.3 | colour strength | free | `warmth` |
| `warmth` | −0.5–0.5 | colour temperature of the grade | free | the lamps' kelvin |
| `bloom` | 0–0.6 | glow around bright things | 6 half-size passes down and back up | `exposure`: bloom reads as haze when the scene is overexposed |
| `grain` | 0–0.08 | film grain | free | grain hides banding and Ultra noise; too much reads as "unfinished" |
| `vignette` | 0–1 | darker corners | free | — |
| `bars` | 0 or 2.39 | letterbox, cinema frame | free (fewer pixels shown) | — |
| halation, chromatic aberration | (in the look presets) | lens and film character | free | — |
| auto exposure | on unless the camera sets `exposure` | the eyes adjusting, from sunlight to a candle (1e-7 to 5000, speed 3) | one tiny compute pass | `fog`: thick fog plus auto exposure can look grey and flat |
| `fog` density | 0–0.06 | light scattering in the air | per-pixel maths, cheap | **with auto exposure, dense fog looks grey; keep under 0.03 unless it's a storm** |

---

## 4. What a scene costs (for whoever writes scenes)

| Scene choice | Cost | Rule of thumb |
|---|---|---|
| Number of things | each is drawn twice (sun shadow and scene); people are about 16 things each | under 800 things for Phone |
| People | about 16 shapes and a skeleton each; CPU per frame | under 50 on screen at once |
| Lamps | each lights every pixel it reaches; on High the two that matter most near the camera also cast ray-traced shadows | under 30 lamps; flicker is now free; glowing things and glass never block lamp light, so rain and neon cost shadows nothing |
| Glass and water | drawn after everything else, can't hide what's behind | keep big glass panes few |
| Fog | cheap, but greys the picture | see above |
| `every 1 / 30 s` scripts | CPU per call; changing a shape's material re-reads the whole world | change materials rarely; change positions freely |
| New shapes while playing | a full re-read of the world | make shapes at the start and hide them (`visible = no`) |

---

## 5. Measuring: the benchmark and the tracker

Every change to this sheet gets judged by numbers we keep.

1. **Run a benchmark.** In the player, open **Benchmark…** and pick **This scene** or **The whole story** (about 30 minutes). It plays by itself: it presses Use, answers questions with the first option, and moves on scene by scene. Or open it directly with `?bench=scene` or `?bench=story`, plus `&quality=0|1|2`, `&choices=0,1,1` and `&seconds=N` (a time limit per scene).
2. **Read the results.** At the end it shows a table, one row per scene, and offers **Copy results** and **Save file**. The last 20 runs are also kept in that browser.
3. **Keep them.** Paste the results into the chat with Claude, or send the file. Claude runs `tools/perf_add.py`, which keeps every run whole in `perf/raw/` and rebuilds `perf/runs.json` and `perf/segments.json`. Those feed the **Smoothness Tracker** (the link is in the results panel).

What each number means:

| Number | What it measures | Target |
|---|---|---|
| fps | frames that reached the screen per second | 60 |
| Worst 1% (1% low) | the frame rate of the slowest 1% of frames: what the worst moments feel like | 55 or more |
| p99 ms | 99% of frames took less than this | 20 ms or less (60 fps is 16.7 ms) |
| Max ms | the single slowest frame | under 50 ms |
| Hitches | frames that took more than twice the usual time and over 25 ms: the stutters you notice | none |
| Hitch % | share of the time spent inside hitches | 0 |
| Dropped | browser frames the GPU had no room for | low |
| GPU ms | estimated GPU time per frame (from the GPU's done signals) | under 14 ms at 60 fps |
| CPU ms | time in script, world and sending to the GPU | under 4 ms |
| Res | the share of full resolution the player chose | as high as the targets allow |
| Load ms | from the scene's script starting to its first frame on screen | under 1500 |

The results also record the build, the GPU's name, the browser, the screen and the pixel density, so runs from different machines and builds can be compared.

## 6. Switches for testing

| Switch | What it does |
|---|---|
| `?quality=0\|1\|2` | Phone, High, Ultra |
| `?msaa=1\|4` | anti-aliasing off or 4× |
| `?skip=N` | skip passes (add up): 1 sun shadow, 2 scene, 4 exposure, 8 bloom, 16 film, 32 shadow pass, 64 scene pass, 128 depth, 256 draw off-screen, 512 no lamp shadows |
| `?prepass=1` | the depth pre-pass on High |
| `?res=0.2…1` | hold the resolution (no automatic changes), to compare costs fairly |
| `?lamps=N&rays=N` | lamp shadows: how many lamps cast them, rays per big lamp (soft edges need 4) |
| `?view=1\|2\|3\|4` | debug views: raw light, exposure, bloom, ray work (blue little, red 200+ steps per pixel) |
| `?ui=0` | hide the controls |
| `?bench=scene\|story` | the benchmark (above) |
| tap the stats line | a live graph of the last 240 frames' times, with 16.7 ms and 33 ms lines |
| `loop play first.loop` | plays a whole story with no picture, for testing the story itself |

---

## 7. Performance tricks: what we have, what's next

Ranked by how much they help smoothness. **Done** means it's in the engine now.

### Frame pacing and freezes (feel first)

| Trick | Status | What it buys |
|---|---|---|
| Only re-read what changed (moves, lamps, world) | **Done** | removed a full GPU rebuild every frame |
| Frame gate (2 in flight) + watchdog that ignores late signals | **Done** | low delay; no permanent freezes, no runaway queue |
| Lost-GPU recovery | **Done** | a driver reset no longer means a black screen for good |
| Loading screen until the first frame | **Done** | no silent black screen while a scene builds |
| Fixed script clock with no catch-up spiral | **Done** | slow frames don't snowball |
| Honest fps (frames shown, not browser ticks) | **Done** | we can trust the number |
| **Audio on its own thread (AudioWorklet)** | Next | the sound mixer now runs on the main thread 23 times a second and can steal a frame; moving it off removes a source of hitches |
| **Pipelines compiled in the background** (`createRenderPipelineAsync`), warmed at load | Next | no hitch the first time a pass or quality is used |
| **Scene loading off the main thread** (a worker builds meshes and ray trees) | Next | no freeze when a scene loads or `go_to` runs |
| Frame recorder, benchmark and Smoothness Tracker | **Done** | every frame's time kept as data, per scene, per build, per GPU |
| GPU timing (timestamp queries) to drive the resolution governor | Next | the governor reacts to real GPU milliseconds before frames drop, not after |
| Script and world in a worker, WASM threads and SIMD | Later | more CPU headroom; GitHub Pages needs a service-worker trick for threads |
| Late camera update (read input just before drawing) | Later | less delay between the hand and the view |

### Fewer pixels, same picture (the biggest GPU wins)

| Trick | Status | What it buys |
|---|---|---|
| Automatic resolution, free to change (viewport into full-size targets) | **Done** | holds the frame rate on any GPU, with no hitch when it changes |
| Lower pixel density caps (Phone 1×, High 1.5×, Ultra 1×) | **Done** | up to 2.25× fewer pixels than before for almost the same look |
| **Temporal upscaling** (render about 65% and rebuild full detail from past frames, like FSR 2 / TAAU), replacing MSAA | Next, the biggest single win | about 2× faster at nearly the same sharpness; also smooths shimmering that MSAA can't |
| Half-resolution lamp shadows, rebuilt with an edge-aware upsample and blended over frames | Next | lamp shadows at about a quarter of the cost |
| Smaller HDR format (RG11B10) | Next | less memory traffic for every pass |
| Bloom and the film in fewer passes | Later | fewer full-screen passes |

### Less work per pixel

| Trick | Status | What it buys |
|---|---|---|
| Brightest-first lamp shadows | **Done** | shadow rays where they show |
| Ray code compiled out when no lamp casts shadows | **Done** | measured: halves Phone's pixel cost (a shader keeps registers for its heaviest path even when it never runs) |
| Shadow rays skip glowing things and glass entirely (left out of the tree) | **Done** | measured: rain-heavy scenes went from over 8 s to 1.5 s a frame on the test GPU |
| Ray trees built only when used (High's lamp shadows, Ultra) | **Done** | Phone loads scenes faster and does no ray work per frame |
| Tree walk: test both children first, nearer first, read only needed fields, stacks made once | **Done** | same shadows, less work; helps Ultra most |
| **Clustered lights** (a compute pass sorts lamps into screen tiles; each pixel loops only its tile's lamps) | Next | scenes with 30+ lamps (markets, the Undercity) cost little more than scenes with 3 |
| Depth pre-pass (shade each pixel once, not once per overlapping thing) | Built, off (`?prepass=1`) | measured no gain yet; needs `@invariant` positions before it's safe on Apple GPUs |
| MSAA samples discarded after the resolve | **Done** | the 4 samples per pixel were being written to memory every frame (costly on phones) |
| Cached sun shadows (redraw only what moved) | Later | the shadow pass nearly disappears in still scenes |

### Less work per object

| Trick | Status | What it buys |
|---|---|---|
| Instancing (same shape drawn at once) | **Done** | few draw calls |
| GPU culling (skip what's off screen or hidden, in a compute pass) | Next | big outdoor scenes and crowds |
| Level of detail for people and trees; flat cut-outs far away | Later | crowds and forests at distance |
| Ray tree refit instead of rebuild when things move | Later | cheaper movement for Ultra and lamp shadows |

### Ultra (cleaner, faster path tracing)

| Trick | Status | What it buys |
|---|---|---|
| History that only shortens on real movement | **Done** | Ultra settles to a clean picture instead of staying grey |
| No flip-flopping between sizes | **Done** | no repeated restarts |
| Reprojection with motion vectors (SVGF-style) | Next | a clean picture while the camera moves |
| Many-light sampling (ReSTIR) | Later | dozens of lamps with little noise |
| Blue-noise sampling | Later | noise that looks like film grain instead of grey speckle |

---

## 8. What went wrong on 4 November, and why

**The root cause (found by building each of the day's versions and running them at phone size):** the morning's lamp-shadow change (`7346c7f`) put ray tracing into every pixel of every quality, Phone too (3 lamps x 4 rays), making each frame about ten times heavier. That alone is "slow", not "broken". What made it never come back was the automatic resolution: it looked for frames the browser asked for but we had to skip, and kept time by adding up frame times capped at 0.1 s. An overloaded GPU makes the browser stop asking for frames, so nothing was ever "skipped", and at one frame every 10 seconds its one-second clock took over a minute to tick once. So the control built to rescue a slow GPU never fired, on any quality, in any scene. It now keeps real time and reads the GPU's own times (and Phone no longer traces rays at all).

The other problems that day, all fixed:

1. **The tests were blind.** Headless Chromium loses the WebGPU device as soon as a canvas is configured, so the GPU never said a frame was done. Every frame came from the watchdog, and the slow numbers were blamed on the software GPU. Now: test on a virtual display (`xvfb-run -a node tools/web_check.js …`).
2. **Ultra's buffers were made on every quality** at every resize (about 80 bytes a pixel), and at laptop sizes on Ultra they passed WebGPU's 128 MB default limit: every frame was invalid, a black screen.
3. **Automatic resolution re-made everything** (canvas, surface and every target) on each step, about once every few seconds, and the old ones weren't freed until garbage collection: hundreds of MB piling up, then the GPU taken away.
4. **Lamp shadow rays were far too many**: up to 6 lamps × 4 rays for every pixel, including hidden ones, through rain and neon that can't cast shadows at all. One ray per pixel costs about as much as all the other lighting.
5. **Nothing recovered from a lost GPU**: the watchdog kept drawing to a dead device every 1.5 s, which is the "freeze, then a frame, then freeze" you saw.
6. **The ray buffer grew by half at every scene change** (it kept the old scene's size as its starting point), so a story played through passed the GPU's limit after several scenes.

## 9. Adding to this sheet

For a new setting or trick, add a row to the right table:

| Setting | Values per level | What it does | Cost | Tune with |
|---|---|---|---|---|

Then benchmark it in three scenes (a small room like `cabin`, a crowd like `ardane_02_market`, a big outdoor one like `ardane_12_garden`) on Phone and High, and write what changed.
