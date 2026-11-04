# The tuning sheet

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
| Pixel density cap | 1.5× | 2× | 2× | the most pixels per screen point we draw | grows with the square: 2× is 4 times the pixels of 1× | automatic resolution (it scales this down) |
| Anti-aliasing (MSAA) | 4× | 4× | — | smooth edges | memory and fill rate, ×4 on the scene pass | pixel density: at 2×, MSAA matters less |
| Sun shadow map | 2048² | 2048² | 4096² | sharpness of sun shadows | one depth pass of every object | shadow area |
| Sun shadow area | 25 m | 40 m | (traced) | how far around the camera sun shadows reach | wider area = blurrier shadow at the same map size | shadow map size |
| Sun shadow softness | 12 taps | 12 taps | (traced) | soft, filtered edges | 12 texture reads per pixel | — |
| Lamp shadows (ray-traced) | 3 lamps | 6 lamps | every lamp | real shadows from lamps, the brightest near the camera first | **the most expensive live setting**: a ray through the scene per lamp per pixel | number of lamps in the scene, resolution |
| Lamps lit per pixel | up to 256 | up to 256 | 1 random lamp per bounce | every lamp lights every pixel it reaches | small per lamp, but adds up past ~30 | clustered lights (planned) |
| Bounces | 1 (plus sky) | 1 (plus sky) | 5 | light bouncing off walls (Ultra is a path tracer) | Ultra: a full ray path per pixel per frame | denoiser, resolution |
| Denoiser | — | — | 4 passes (à-trous) | turns Ultra's noise into a clean picture | 4 blur passes | history length |
| Ultra history | — | — | grows; 8 frames when things move | how many frames are averaged into one | none (it's how clean Ultra gets) | motion: a longer history smears moving things |
| Lowest automatic resolution | 50% | 50% | 35% | how far the player may drop to keep up | — | pixel density cap |

---

## 2. Automatic controls (the player tunes itself)

| Control | Now | What it does | Why |
|---|---|---|---|
| **Automatic resolution** | checks once a second: drops 15% (30% if badly behind) when more than 12% of frames were dropped; climbs 10% after 4 calm seconds, never past a size that was too slow; tries higher again every 30 s | keeps the frame rate up by drawing fewer pixels | the GPU is the bottleneck in every scene we've measured (CPU is under 1 ms a frame) |
| **Frame gate** | at most 2 frames waiting on the GPU | a slow GPU drops a frame instead of falling seconds behind | keeps delay low |
| **Watchdog** | 90 dropped frames in a row → assume the GPU's "done" signal was lost and carry on | stops a permanent freeze | hidden tabs and lost callbacks |
| **Script clock** | 120 steps a second, at most 4 per frame; extra time is dropped | scripts and people move smoothly at any frame rate | a slow frame can't snowball into slower frames |
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
| Lamps | each lights every pixel it reaches; the first 3 or 6 also cast ray-traced shadows | under 30 lamps; flicker is now free |
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
| `?skip=N` | skip passes (add up): 1 sun shadow, 2 scene, 4 exposure, 8 bloom, 16 film, 32 shadow pass, 64 scene pass, 128 depth, 256 draw off-screen (headless tests), 512 no lamp shadows |
| `?view=1\|2\|3` | debug views: raw light, exposure, bloom |
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
| Frame gate (2 in flight) + watchdog | **Done** | low delay; no permanent freezes |
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
| Automatic resolution | **Done** | holds the frame rate on any GPU |
| **Temporal upscaling** (render about 65% and rebuild full detail from past frames, like FSR 2 / TAAU), replacing MSAA | Next, the biggest single win | about 2× faster at nearly the same sharpness; also smooths shimmering that MSAA can't |
| Half-resolution lamp shadows, rebuilt with an edge-aware upsample and blended over frames | Next | lamp shadows at about a quarter of the cost |
| Smaller HDR format (RG11B10) | Next | less memory traffic for every pass |
| Bloom and the film in fewer passes | Later | fewer full-screen passes |

### Less work per pixel

| Trick | Status | What it buys |
|---|---|---|
| Brightest-first lamp shadows | **Done** | shadow rays where they show |
| **Clustered lights** (a compute pass sorts lamps into screen tiles; each pixel loops only its tile's lamps) | Next | scenes with 30+ lamps (markets, the Undercity) cost little more than scenes with 3 |
| Depth pre-pass (shade each pixel once, not once per overlapping thing) | Next | saves the expensive shading on hidden surfaces |
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

## 8. Adding to this sheet

For a new setting or trick, add a row to the right table:

| Setting | Values per level | What it does | Cost | Tune with |
|---|---|---|---|---|

Then benchmark it in three scenes (a small room like `cabin`, a crowd like `ardane_02_market`, a big outdoor one like `ardane_12_garden`) on Phone and High, and write what changed.
