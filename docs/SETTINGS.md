# Every setting

Made by `node tools/settings_doc.js > docs/SETTINGS.md` from the editor's settings
(`web/editor/settings_schema.js`) and the engine's (`loop settings list`): don't edit by hand.

## How settings work

An engine setting has one name everywhere: in the editor (Project Settings), in a project's
`project.json` and in a scene's script. Values come in three layers, the higher winning:

1. **The editor**: what you change while editing (kept in your browser).
2. **The scene**: what its script asks for, with `setting "physics.iterations", 24`,
   `settings {story_textSpeed: 20, "audio.reverb": 1.5}` (`_` or `.` between the parts), or a world word
   (`gravity(…)`, `look(…)` …). A new scene starts from the project's values again.
3. **The project**: `projects/<name>/project.json`, `"settings": {"render.lamps": 4}` (the editor writes it:
   File → Save to project). `loop render` and the site build read it too.

`get_setting("player.walk")` reads a setting's value in effect.

Statuses: **live** changes the picture or the world as you set it (**· script**: a scene can set it too);
**editor** is the editor's own; **project.json** is read by the site build; **not built yet** is a feature on the
roadmap (docs/TUNING.md); **needs you** is your call.


# Project settings

## General

### Project

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Title | `project.title` | "" |  | project.json |  | Shown in the player's scene list and the editor. Saved in project.json. |
| Description | `project.description` | "" |  | project.json |  | One or two sentences about the project. |
| Start scene | `project.start` | "" |  | project.json |  | The scene the project opens with. |
| Mode for scenes that don't say | `project.mode` | "" | "" / movie | live · script | `setting "project.mode", ""` · `mode "movie"` | mode for scenes that don't say: game \| movie |

### Folders

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Scenes folder | `project.scenesDir` | . |  | project.json |  | Relative to the project folder. |
| Trailer stills folder | `project.stillsDir` | stills |  | project.json |  |  |
| Shared scripts folder | `project.libraryDir` | . |  | project.json |  | Where `use "name"` scripts are. |

## Rendering

### Quality

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Live quality | `render.quality` | 1 | 0 / 1 / 2 | live |  | Phone: 1x pixels, sun shadows, lamps without shadows. High: sharper, crisp shadows from the two lamps that matter most. Ultra: path traced on the GPU. |
| Target frame rate | `render.targetFps` | 60 | 30 / 60 / 90 / 120 / 144 | live · script | `setting "render.targetFps", 60` | What the automatic resolution aims for (never above the display's own rate). |
| Frames in flight | `render.framesInFlight` | 2 | 1 / 2 / 3 | live · script | `setting "render.framesInFlight", 2` | More hides GPU stalls; fewer means less delay. |
| Stop drawing when the page is hidden | `render.pauseHidden` | true |  | live · script | `setting "render.pauseHidden", true` | stop drawing while the page is hidden |

### Resolution

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Hold resolution | `render.res` | 0 | 0 … 1 | live · script | `setting "render.res", 0` | 0 = automatic (climbs while the GPU keeps up, drops when it doesn't). Any other value holds it there, to compare costs fairly. |
| Pixel density cap: Phone | `render.ratioPhone` | 1 | 0.5 … 3 x | live · script | `setting "render.ratioPhone", 1` | most pixels per screen point on Phone |
| Pixel density cap: High | `render.ratioHigh` | 1.5 | 0.5 … 3 x | live · script | `setting "render.ratioHigh", 1.5` | most pixels per screen point on High |
| Pixel density cap: Ultra | `render.ratioUltra` | 1 | 0.5 … 3 x | live · script | `setting "render.ratioUltra", 1` | most pixels per screen point on Ultra |
| Lowest automatic resolution | `render.floor` | 0.5 | 0.35 … 1 | live · script | `setting "render.floor", 0.5` | lowest automatic resolution (Phone and High) |
| Lowest automatic resolution (Ultra) | `render.floorUltra` | 0.35 | 0.35 … 1 | live · script | `setting "render.floorUltra", 0.35` | lowest automatic resolution on Ultra |
| Too slow when the GPU takes | `render.slowGpu` | 110 | 50 … 300 % of a frame | live · script | `setting "render.slowGpu", 110` | Over this share of the frame budget: drop the resolution. |
| Climb if the next size fits under | `render.climbUnder` | 100 | 30 … 150 % of a frame | live · script | `setting "render.climbUnder", 100` | climb only if the bigger size would still fit under this % of the budget |
| Steady time before climbing | `render.calm` | 3 | 0.5 … 30 s | live · script | `setting "render.calm", 3` | seconds of steady frames before the resolution climbs |
| Temporal upscaling (TAAU) | `render.upscaler` | off | off / on | not built yet |  | Render about 65% and rebuild full detail from past frames: the biggest speed-up on the roadmap. |

### Anti-aliasing

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| MSAA | `render.msaa` | 4 | 1 / 4 | live · script | `setting "render.msaa", 4` | Multisampling of the scene pass. Changes live (the pipelines are rebuilt). |
| Edge smoothing (FXAA) | `render.fxaa` | false |  | live · script | `setting "render.fxaa", false` | Cheap smoothing of hard edges in the film pass; most useful with MSAA off. |
| Film grain while playing | `render.filmGrain` | false |  | live · script | `setting "render.filmGrain", false` | Off: the live picture never fizzes. Stills and film frames always keep a scene's grain. |
| Temporal anti-aliasing | `render.taa` | false |  | not built yet |  | Comes with temporal upscaling. |

### Sun shadows

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Sun shadows | `render.sunShadows` | true |  | live · script | `setting "render.sunShadows", true` | the sun casts shadows |
| Shadow map: Phone | `render.sunMapPhone` | 1024 | 512 / 1024 / 2048 / 4096 | live · script | `setting "render.sunMapPhone", 1024` | sun shadow map size on Phone |
| Shadow map: High | `render.sunMapHigh` | 2048 | 1024 / 2048 / 4096 / 8192 | live · script | `setting "render.sunMapHigh", 2048` | sun shadow map size on High |
| Shadow area | `render.shadowDistance` | 0 | 0 … 300 | live · script | `setting "render.shadowDistance", 0` | Half-width of the area with sun shadows. Bigger reaches further but blurs (0 = Phone 25 m, High 40 m). |
| Depth bias | `render.shadowBias` | 0.0015 | 0 … 0.02 | live · script | `setting "render.shadowBias", 0.0015` | Higher hides speckled "acne", lower keeps shadows touching what casts them. |
| Softness | `render.shadowSoftness` | 1.8 | 0 … 8 texels | live · script | `setting "render.shadowSoftness", 1.8` | blur radius of sun shadow edges (in shadow-map texels) |
| Cascades | `render.cascades` | 1 | 1 / 2 / 3 / 4 | not built yet |  | Several shadow maps by distance: sharp near, wide far. |
| Cache still shadows | `render.cachedShadows` | false |  | not built yet |  | Redraw only what moved. |

### Lamp shadows

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Lamp shadows | `render.lampShadows` | true |  | live · script | `setting "render.lampShadows", true` | lamps cast shadows at all |
| Lamps that cast shadows | `render.lamps` | -1 | -1 … 16 | live · script | `setting "render.lamps", -1` | -1 = the quality's own (Phone 0, High 2). Each one costs about as much as all other lighting. |
| Rays per lamp | `render.rays` | -1 | -1 … 16 | live · script | `setting "render.rays", -1` | Soft edges need about 4. |
| Half-resolution lamp shadows | `render.lampHalfRes` | false |  | not built yet |  | About a quarter of the cost, rebuilt with an edge-aware upsample. |
| Contact shadows | `render.contactShadows` | false |  | not built yet |  | Short screen-space rays for small creases. |

### Lighting

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Most lamps drawn | `render.maxLights` | 256 | 1 … 256 | live · script | `setting "render.maxLights", 256` | The brightest near the camera are kept. |
| Light from the sky | `render.skyLight` | 1 | 0 … 4 x | live · script | `setting "render.skyLight", 1` | strength of the light from the sky (bounced daylight) |
| Shading in creases and undersides | `render.occlusion` | 1 | 0 … 2 x | live · script | `setting "render.occlusion", 1` | how much creases and undersides darken |
| Clustered lights | `render.clustered` | false |  | not built yet |  | Lamps sorted into screen tiles, so 30+ lamps cost little more than 3. |
| Screen-space ambient occlusion | `render.ao` | off | off / SSAO / GTAO | not built yet |  | Real contact darkening from the depth buffer. |
| Reflections | `render.reflections` | sky | sky / ssr | not built yet |  | Screen-space reflections. |
| Bounced light from probes | `render.gi` | off | off / on | not built yet |  | Light probes for bounced light in the live picture. |

### Passes

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Scene lighting | `render.passScene` | true |  | live · script | `setting "render.passScene", true` | debug: the scene's lighting |
| Exposure | `render.passExposure` | true |  | live · script | `setting "render.passExposure", true` | debug: automatic exposure |
| Bloom | `render.passBloom` | true |  | live · script | `setting "render.passBloom", true` | debug: bloom |
| Film (tone curve, grading, grain) | `render.passFilm` | true |  | live · script | `setting "render.passFilm", true` | debug: the film pass (tone curve, grading, grain) |
| Shadow pass | `render.passShadow` | true |  | live · script | `setting "render.passShadow", true` | debug: the shadow pass |
| Scene pass | `render.passDraw` | true |  | live · script | `setting "render.passDraw", true` | debug: the scene pass |
| Depth pre-pass | `render.prepass` | false |  | live · script | `setting "render.prepass", false` | Shade each pixel once. Measured no gain yet; needs @invariant positions before it's safe on Apple GPUs. |

### Debug

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Debug view | `render.view` | 0 | 0 / 1 / 2 / 3 / 4 | live · script | `setting "render.view", 0` | debug view: 0 picture, 1 raw light, 2 exposure, 3 bloom, 4 ray work |
| Light icons | `render.showLights` | false |  | editor |  | Drawn over the viewport by the editor. |
| Bounding boxes | `render.showBounds` | false |  | editor |  | Drawn over the viewport by the editor. |
| Wireframe | `render.wireframe` | false |  | not built yet |  | WebGPU has no line mode: needs its own pipeline. |

## Ultra (path tracing)

### Sampling

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Bounces | `ultra.bounces` | 5 | 1 … 16 | live · script | `setting "ultra.bounces", 5` | light bounces per path |
| Paths per pixel per frame | `ultra.spp` | 1 | 1 … 16 | live · script | `setting "ultra.spp", 1` | Cleaner each frame, slower each frame. |
| Most frames averaged | `ultra.history` | 0 | 0 … 1024 | live · script | `setting "ultra.history", 0` | A limit keeps the picture following slow changes; 0 keeps sharpening while still. |
| Firefly clamp | `ultra.clamp` | 4 | 0.5 … 50 | live · script | `setting "ultra.clamp", 4` | Brightest a single path may be (lower: fewer sparkles, a little darker highlights). |
| Many-light sampling (ReSTIR) | `ultra.restir` | false |  | not built yet |  | Dozens of lamps with little noise. |
| Blue-noise sampling | `ultra.blueNoise` | false |  | not built yet |  | Noise that looks like film grain. |

### Noise removal

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Noise removal | `ultra.denoise` | true |  | live · script | `setting "ultra.denoise", true` | noise removal |
| Strength | `ultra.denoisePasses` | 4 | 1 … 4 passes | live · script | `setting "ultra.denoisePasses", 4` | noise removal strength (passes) |
| Reprojection while moving (SVGF) | `ultra.motionVectors` | false |  | not built yet |  | A clean picture while the camera moves. |

### Features

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Glossy reflections | `ultra.glossy` | true |  | live · script | `setting "ultra.glossy", true` | glossy reflections |
| Glass and refraction | `ultra.glass` | true |  | live · script | `setting "ultra.glass", true` | glass and refraction |
| Depth of field | `ultra.dof` | true |  | live · script | `setting "ultra.dof", true` | depth of field |
| Traced sun shadows | `ultra.sunShadows` | true |  | live · script | `setting "ultra.sunShadows", true` | traced sun shadows |
| Shadows from every lamp | `ultra.lampShadows` | true |  | live · script | `setting "ultra.lampShadows", true` | shadows from every lamp |

## Cinema renderer

### Stills and films (loop render)

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Quality | `cinema.quality` | "" | "" / draft / good / cinema / ultra | live · script | `setting "cinema.quality", ""` · `render(quality: "cinema")` | Read by `loop render` from project.json, and by render() and film() in scripts. |
| Width | `cinema.width` | 0 | 0 … 8192 px | live · script | `setting "cinema.width", 0` | 0 = render() 1920, film() 1280. |
| Height | `cinema.height` | 0 | 0 … 8192 px | live · script | `setting "cinema.height", 0` | 0 = 16:9 of the width. |
| Samples per pixel | `cinema.samples` | 0 | 0 … 65536 | live · script | `setting "cinema.samples", 0` | samples per pixel (0 = from the quality) |
| Bounces | `cinema.bounces` | 10 | 1 … 64 | live · script | `setting "cinema.bounces", 10` | light bounces |
| Noise removal | `cinema.denoise` | true |  | live · script | `setting "cinema.denoise", true` | noise removal |
| Rainbow dispersion in glass | `cinema.dispersion` | true |  | live · script | `setting "cinema.dispersion", true` | rainbow dispersion in glass |
| Motion blur | `cinema.motionBlur` | true |  | live · script | `setting "cinema.motionBlur", true` | motion blur |
| Film frames per second | `cinema.fps` | 24 | 1 … 120 | live · script | `setting "cinema.fps", 24` · `film(fps: 24)` | film frames per second |
| Film output folder | `cinema.folder` | frames |  | live · script | `setting "cinema.folder", "frames"` | folder for film frames |
| Threads | `cinema.threads` | 0 | 0 … 256 | live · script | `setting "cinema.threads", 0` | render threads (0 = all) |

## Post-process / Film

### Look

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Look | `look.preset` | "" | "" / natural / blockbuster / noir / golden / bleach / seventies / neon / dream / documentary / horror / arctic / western | live | `look "noir"` |  |
| Tone curve | `look.tonemap` | agx | agx / aces / filmic / linear | live | `look(tone: "aces")` |  |
| Contrast | `look.contrast` | 1 | 0 … 2 | live | `look(contrast: 1.2)` |  |
| Saturation | `look.saturation` | 1 | 0 … 2 | live | `look(saturation: 0.8)` |  |
| Black and white | `look.monochrome` | false |  | live | `look(monochrome: yes)` |  |

### White balance

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Warmth | `look.temperature` | 0 | -1 … 1 | live | `look(warmth: 0.3)` | Blue to amber. |
| Tint | `look.tint` | 0 | -1 … 1 | live | `look(tint: 0.1)` | Green to magenta. |

### Grading

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Lift (shadows) | `look.lift` | (0, 0, 0) | -0.2 … 0.2 | live | `look(lift: vec(0, 0, 0.02))` |  |
| Gamma (mids) | `look.gamma` | (1, 1, 1) | 0.5 … 2 | live | `look(gamma: vec(1, 1, 1.05))` |  |
| Gain (highlights) | `look.gain` | (1, 1, 1) | 0.5 … 2 | live | `look(gain: …)` |  |
| Split tone: shadows | `look.shadowTint` | (0, 0, 0) | -0.2 … 0.2 | live | `look(shadow_tint: …)` |  |
| Split tone: highlights | `look.highlightTint` | (0, 0, 0) | -0.2 … 0.2 | live | `look(highlight_tint: …)` |  |
| Faded blacks | `look.blackLevel` | 0 | 0 … 0.2 | live | `look(black_level: 0.03)` |  |

### Glow

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Bloom | `look.bloom` | 0.04 | 0 … 0.5 | live | `look(bloom: 0.1)` |  |
| Bloom size | `look.bloomRadius` | 0.02 | 0.005 … 0.08 | live | `look(bloom_size: 0.03)` |  |
| Halation (red film glow) | `look.halation` | 0 | 0 … 1 | live | `look(halation: 0.3)` |  |

### Lens

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Vignette | `look.vignette` | 0.25 | 0 … 1 | live | `look(vignette: 0.4)` |  |
| Colour fringes | `look.chromaticAberration` | 0 | 0 … 6 px | live | `look(fringes: 1.5)` |  |
| Distortion | `look.distortion` | 0 | -0.3 … 0.3 | live | `look(distortion: 0.1)` | + barrel, − pincushion. |
| Sharpen | `look.sharpen` | 0 | 0 … 1 | live | `look(sharpen: 0.3)` |  |

### Film stock

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Grain | `look.grain` | 0 | 0 … 0.3 | live | `look(grain: 0.05)` |  |
| Grain size | `look.grainSize` | 1 | 0.5 … 4 | live | `look(grain_size: 2)` |  |
| Cinema bars | `look.letterbox` | 0 | 0 / 1.85 / 2 / 2.39 / 2.76 | live | `look(bars: 2.39)` |  |

## Camera & exposure

### Exposure

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Automatic exposure | `cam.autoExposure` | true |  | live | `camera(auto_exposure: yes)` | Meter the scene, like an eye. |
| Exposure compensation | `cam.exposureCompensation` | 0 | -5 … 5 stops | live | `camera(exposure: 1)` |  |
| ISO | `cam.iso` | 100 | 25 … 102400 | live | `camera(iso: 400)` |  |
| Shutter | `cam.shutter` | 0.0208 | 0.0001 … 1 s | live | `camera(shutter: 1/48)` |  |
| Eye adaptation speed | `cam.adaptSpeed` | 1 | 0.1 … 10 x | live · script | `setting "cam.adaptSpeed", 1` | how fast exposure follows the light (like an eye) |
| Darkest it adapts to | `cam.minEv` | -12 | -16 … 10 EV | live · script | `setting "cam.minEv", -12` | darkest scene exposure will adapt to (EV) |
| Brightest it adapts to | `cam.maxEv` | 23 | 0 … 26 EV | live · script | `setting "cam.maxEv", 23` | brightest scene exposure will adapt to (EV) |

### Lens

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Focal length | `cam.focalLength` | 35 | 8 … 300 mm | live | `camera(lens: 50 mm)` |  |
| Aperture | `cam.fStop` | 2.8 | 0.95 … 32 f/ | live | `camera(f_stop: 1.8)` |  |
| Focus distance | `cam.focusDistance` | 0 | 0 … 100 m | live | `camera(focus: 3 m)` |  |
| Aperture blades | `cam.blades` | 7 | 0 … 16 | live | `camera(blades: 9)` | Shapes the out-of-focus highlights in cinema renders. |
| Sensor | `cam.sensor` | ff | ff / s35 / apsc / m43 / imax | live | `camera(sensor: "s35")` |  |

### Motion

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Handheld shake (walking) | `cam.shake` | 0 | 0 … 1 | live · script | `setting "cam.shake", 0` | handheld shake of the walking camera |
| Look smoothing | `cam.smoothing` | 18 | 1 … 60 | live · script | `setting "cam.smoothing", 18` | How quickly the view eases to where you point it (higher = snappier). |
| Live motion blur | `cam.motionBlur` | false |  | not built yet |  | Needs motion vectors. |

## Sky, sun & fog

### Fog

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Density | `fog.density` | 0 | 0 … 0.2 | live | `fog(0.02)` | Per metre. 0.01 is a light haze. |
| Colour | `fog.albedo` | (0.9, 0.9, 0.9) | 0 … 1 | live | `fog(color: …)` |  |
| God rays (forward scattering) | `fog.anisotropy` | 0.6 | -0.9 … 0.95 | live | `fog(glow: 0.8)` |  |
| Fog top | `fog.top` | 1000000 | 0 … 1000000 m | live | `fog(top: 8 m)` |  |

### Sky

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Sky | `sky.kind` | physical | physical / color / none | live | `sky "physical"` |  |
| Haze | `sky.turbidity` | 2.5 | 1.7 … 10 | live | `sky(haze: 4)` | 2 clear … 10 smoggy. |
| Brightness | `sky.intensity` | 1 | 0 … 4 | live | `sky(brightness: 1.5)` |  |
| Stars | `sky.stars` | 0 | 0 … 1 | live | `sky(stars: 0.02)` |  |
| Flat sky colour | `sky.color` | #6699ff |  | live | `sky("color", color: …)` |  |
| Ground colour | `sky.ground` | (0.3, 0.3, 0.3) | 0 … 1 | live | `sky(ground: vec(0.2, 0.25, 0.15))` |  |

### Sun

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Elevation | `sun.elevation` | 45 | -10 … 90 ° | live | `sun(elevation: 30 deg)` |  |
| Toward | `sun.azimuth` | 180 | 0 … 360 ° | live | `sun(toward: 135 deg)` |  |
| Brightness | `sun.lux` | 100000 | 0 … 150000 lux | live | `sun(lux: 80000)` |  |
| Size in the sky | `sun.size` | 0.53 | 0.1 … 10 ° | live | `sun(size: 1 deg)` | Wider: softer shadows. Our sun is 0.53°. |
| Day cycle | `sun.dayCycle` | false |  | live |  | The sun goes round on its own. |
| Day length | `sun.daySpeed` | 24 | 0.5 … 1440 min | live |  |  |

## World

### Distance and detail

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Draw distance | `world.drawDistance` | 4000 | 50 … 20000 m | live · script | `setting "world.drawDistance", 4000` | farthest the camera sees (m) |
| Pattern detail fade | `world.patternFade` | 1 | 0.25 … 4 x | live · script | `setting "world.patternFade", 1` | Higher fades fine patterns (bricks, grain) sooner with distance: less shimmer. |
| Forest density | `world.foliageDensity` | 1 | 0 … 3 x | live · script | `setting "world.foliageDensity", 1` · `forest(spacing: …)` | Applies when a scene builds (Run). |
| Terrain detail | `world.terrainRes` | 1 | 0.25 … 4 x | live · script | `setting "world.terrainRes", 1` · `terrain(detail: …)` | Applies when a scene builds (Run). |

### Big worlds

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Stream big worlds | `world.streaming` | false |  | not built yet |  | Load and unload cells as you move. |
| GPU culling | `world.gpuCulling` | false |  | not built yet |  | Skip what's off screen or hidden. |
| Flat cut-outs far away | `world.impostors` | false |  | not built yet |  | Trees and people as cards in the distance. |
| Level-of-detail bias | `world.lodBias` | 0 | -2 … 2 | not built yet |  | Needs levels of detail first. |

## Physics

### Simulation

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Gravity | `physics.gravity` | (0, -9.81, 0) | -100 … 100 m/s² | live · script | `setting "physics.gravity", [0,-9.81,0]` · `gravity(vec(0, -1.62, 0))` | gravity (m/s²); `gravity(…)` in a script sets it for the scene |
| Steps per second | `physics.rate` | 120 | 60 / 120 / 240 | live · script | `setting "physics.rate", 120` | physics steps per second |
| Solver iterations | `physics.iterations` | 16 | 1 … 64 | live · script | `setting "physics.iterations", 16` | More: stiffer stacks, slower. |
| Sleep below | `physics.sleep` | 0.05 | 0 … 1 m/s | live · script | `setting "physics.sleep", 0.05` | things slower than this (m/s) for half a second go to sleep |
| Continuous collisions | `physics.ccd` | false |  | not built yet |  | So fast things never pass through thin walls. |

### Defaults

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Friction | `physics.friction` | 0.6 | 0 … 2 | live · script | `setting "physics.friction", 0.6` · `solid(ball, friction: 0.3)` | default friction (solid() and the world) |
| Bounce | `physics.bounce` | 0.2 | 0 … 1 | live · script | `setting "physics.bounce", 0.2` · `solid(ball, bounce: 0.7)` | default bounce (solid() and the world) |

### Walking body

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Radius | `physics.charRadius` | 0.25 | 0.1 … 1 m | live · script | `setting "physics.charRadius", 0.25` | walking body radius (m) |
| Height | `physics.charHeight` | 1.1 | 0.4 … 2.5 m | live · script | `setting "physics.charHeight", 1.1` | height of the walking body's capsule (m) |
| Step height | `physics.charStep` | 0.3 | 0 … 1 m | live · script | `setting "physics.charStep", 0.3` | highest step the walking body climbs (m) |
| Steepest walkable | `physics.charSlope` | 53 | 10 … 85 ° | live · script | `setting "physics.charSlope", 53` | steepest slope it walks up (degrees) |

### Cloth

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Cloth iterations | `physics.clothIterations` | 8 | 1 … 64 | live · script | `setting "physics.clothIterations", 8` | cloth solver iterations |
| Wind | `physics.wind` | (0, 0, 0) | -50 … 50 m/s | live · script | `setting "physics.wind", [0,0,0]` | wind on cloth (m/s) |

## Player & controls

### Movement

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Walk speed | `player.walk` | 1.6 | 0.2 … 10 m/s | live · script | `setting "player.walk", 1.6` | walking speed (m/s) |
| Run speed | `player.run` | 4.5 | 0.5 … 20 m/s | live · script | `setting "player.run", 4.5` | running speed (m/s) |
| Eye height | `player.eye` | 1.6 | 0.3 … 3 m | live · script | `setting "player.eye", 1.6` | eye height (m) |
| Head bob | `player.bob` | 0.025 | 0 … 0.1 m | live · script | `setting "player.bob", 0.025` | head bob while walking (m) |
| Start and stop | `player.accel` | 7 | 1 … 30 | live · script | `setting "player.accel", 7` | how quickly walking starts and stops |

### Looking

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Mouse sensitivity | `player.look` | 1 | 0.1 … 5 x | live · script | `setting "player.look", 1` | mouse look speed |
| Invert vertical look | `player.invertY` | false |  | live · script | `setting "player.invertY", false` | invert vertical look |
| Touch look speed | `player.touchLook` | 1.6 | 0.2 … 5 x | live · script | `setting "player.touchLook", 1.6` | touch look speed |
| Thumb stick size | `player.stickSize` | 110 | 60 … 200 px | live · script | `setting "player.stickSize", 110` | thumb stick size (px) |
| Gamepad dead zone | `player.deadzone` | 0.15 | 0 … 0.5 | live · script | `setting "player.deadzone", 0.15` | gamepad stick dead zone |

### Key bindings

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Forward | `keys.forward` | KeyW |  | live · script | `setting "keys.forward", "KeyW"` | key: forward |
| Back | `keys.back` | KeyS |  | live · script | `setting "keys.back", "KeyS"` | key: back |
| Left | `keys.left` | KeyA |  | live · script | `setting "keys.left", "KeyA"` | key: left |
| Right | `keys.right` | KeyD |  | live · script | `setting "keys.right", "KeyD"` | key: right |
| Run | `keys.run` | ShiftLeft |  | live · script | `setting "keys.run", "ShiftLeft"` | key: run |
| Use / talk | `keys.use` | KeyE |  | live · script | `setting "keys.use", "KeyE"` | key: use / talk |

## Animation & people

### Bodies

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Feet planted with IK | `anim.ik` | true |  | live · script | `setting "anim.ik", true` | feet planted on the ground (IK) |
| Breathing | `anim.breathing` | true |  | live · script | `setting "anim.breathing", true` | people breathe |
| Arm swing | `anim.armSwing` | true |  | live · script | `setting "anim.armSwing", true` | arms swing when walking |
| Time to change speed | `anim.blend` | 0.17 | 0.02 … 2 s | live · script | `setting "anim.blend", 0.17` | how long a person takes to change speed (s) |
| Most people in a scene | `anim.maxPeople` | 200 | 1 … 5000 | live · script | `setting "anim.maxPeople", 200` | most people in a scene |
| Skinned bodies | `anim.skinned` | false |  | not built yet |  | Smooth bodies instead of capsules. |
| Faces | `anim.faces` | false |  | not built yet |  |  |
| Hair | `anim.hair` | false |  | not built yet |  |  |

### Crowds

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Walk around obstacles | `anim.steering` | false |  | not built yet |  | People now walk straight to where they're sent. |
| Simple bodies beyond | `anim.crowdLod` | 30 | 5 … 200 | not built yet |  | Needs levels of detail. |

## Audio

### Volume

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Master | `audio.master` | 1 | 0 … 2 | live · script | `setting "audio.master", 1` | master volume |
| Ambience (fire, wind, rain…) | `audio.ambience` | 1 | 0 … 2 | live · script | `setting "audio.ambience", 1` | endless sounds: fire, wind, rain, birds… |
| Effects and knocks | `audio.effects` | 1 | 0 … 2 | live · script | `setting "audio.effects", 1` | one-off sounds and knocks |
| Footsteps | `audio.footsteps` | 1 | 0 … 2 | live · script | `setting "audio.footsteps", 1` | footsteps |
| Dialogue | `audio.dialogue` | 1 | 0 … 2 | live · script | `setting "audio.dialogue", 1` | For voiced lines (none yet). |
| Music | `audio.music` | 1 | 0 … 2 | live · script | `setting "audio.music", 1` | For music tracks (none yet). |

### Space

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Room echo | `audio.reverb` | 1 | 0 … 3 x | live · script | `setting "audio.reverb", 1` · `room(size: 0.6, echo: 0.3)` | room echo (times the scene's room()) |
| Distance fall-off | `audio.distance` | 1 | 0.1 … 5 x | live · script | `setting "audio.distance", 1` | how fast sounds fade with distance |
| Ear delay (3D) | `audio.earDelay` | true |  | live · script | `setting "audio.earDelay", true` | sound reaches the nearer ear first (3D) |
| Air absorption | `audio.airAbsorb` | true |  | live · script | `setting "audio.airAbsorb", true` | far sounds lose their highs |
| Doppler | `audio.doppler` | false |  | live · script | `setting "audio.doppler", false` | moving sounds change pitch |
| Limiter | `audio.limiter` | true |  | live · script | `setting "audio.limiter", true` | soft limiter: loud moments never clip |

### Output

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Buffer | `audio.buffer` | 2048 | 256 / 512 / 1024 / 2048 / 4096 | live · script | `setting "audio.buffer", 2048` | Smaller is less delay but more risk of crackles. Applies when sound is turned on. |
| Mix on its own thread (AudioWorklet) | `audio.worklet` | false |  | not built yet |  | Removes a source of hitches. |

## Scripting

### Clock

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Script steps per second | `script.tick` | 120 | 60 / 120 / 240 | live · script | `setting "script.tick", 120` | script steps per second |
| Most catch-up per frame | `script.catchUp` | 8 | 1 … 50 ms | live · script | `setting "script.catchUp", 8` | most catch-up time per frame (ms) |
| Most steps per frame | `script.maxSteps` | 12 | 1 … 60 | live · script | `setting "script.maxSteps", 12` | most script steps per frame |
| World speed | `script.timeScale` | 1 | 0 … 4 x | live · script | `setting "script.timeScale", 1` | Slow motion and fast forward (walking keeps real time). |

### Problems

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| On a problem | `script.onError` | continue | continue / stop / pause | live · script | `setting "script.onError", "continue"` | continue \| stop \| pause |
| Warn about unused names | `script.strict` | false |  | not built yet |  |  |

### Testing

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Autoplay (the player counts as near everything) | `script.autoplay` | false |  | live · script | `setting "script.autoplay", false` | the player counts as near everything (tests) |
| Choices picked automatically | `script.choices` | 0 |  | live · script | `setting "script.choices", "0"` | Comma list, used when Autoplay is on (like loop play --choices). |

## Story & UI

### Dialogue

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Reading speed | `story.textSpeed` | 15 | 3 … 60 chars/s | live · script | `setting "story.textSpeed", 15` · `line(…, seconds: 4)` | reading speed for line() (characters per second) |
| Shortest line | `story.lineMin` | 2 | 0.5 … 20 s | live · script | `setting "story.lineMin", 2` | shortest time a line stays (s) |
| Subtitle size | `story.subtitleSize` | 1 | 0.5 … 3 x | live · script | `setting "story.subtitleSize", 1` | subtitle size |
| Show speaker names | `story.speakerNames` | true |  | live · script | `setting "story.speakerNames", true` | show speaker names |
| Speaker colour | `story.speakerColor` | #ffd27a |  | live · script | `setting "story.speakerColor", "#ffd27a"` | speaker name colour |

### Choices

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Time to choose | `story.choiceTimeout` | 0 | 0 … 60 s | live · script | `setting "story.choiceTimeout", 0` | seconds to choose (0 = wait) |
| When time runs out | `story.timeoutPick` | 0 | 0 / 1 / -1 | live · script | `setting "story.timeoutPick", 0` | option picked when time runs out (-1 random) |

### Between scenes

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Fade | `story.fade` | 0.6 | 0 … 5 s | live · script | `setting "story.fade", 0.6` | fade between scenes (s) |
| Loading text | `story.loadingText` | Loading {scene}… |  | live · script | `setting "story.loadingText", "Loading {scene}…"` | {scene} becomes the scene's title. |
| Show the buttons and stats (player) | `story.hud` | true |  | live · script | `setting "story.hud", true` | show the HUD (buttons and stats) |

## Performance & benchmark

### Benchmark

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Seconds per scene | `perf.benchSeconds` | 20 | 5 … 120 s | editor |  |  |
| A hitch is a frame over | `perf.hitch` | 25 | 10 … 100 ms | live · script | `setting "perf.hitch", 25` | Or twice the median frame, whichever is longer. |
| Smoothness Tracker link | `perf.tracker` | "" |  | editor |  | Opened by the benchmark results. |

### Threads

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Build scenes off the main thread | `perf.loadWorker` | false |  | not built yet |  |  |
| Compile GPU pipelines in the background | `perf.asyncPipelines` | false |  | not built yet |  |  |
| WASM threads and SIMD | `perf.wasmThreads` | false |  | not built yet |  | GitHub Pages needs a service-worker trick for threads. |

## Build & publish

### Web build

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Include this project on the site | `build.publish` | true |  | project.json |  |  |
| Include trailer stills | `build.stills` | true |  | project.json |  |  |
| Stamp the build on the pages | `build.stamp` | true |  | project.json |  |  |
| Optimisation | `build.opt` | O3 | O0 / O1 / O2 / O3 / Os / Oz | project.json |  | Read by tools/build_site.sh (LOOP_OPT). |

### Publishing

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Publish to | `build.target` | pages | pages / zip | needs you |  | Where it goes, and when, is your call. |
| Pre-compress (brotli) | `build.compress` | false |  | needs you |  | Needs a host that serves .br files. |
| Emscripten version | `build.emsdk` | 6.0.13 |  | needs you |  | Set in the Pages workflow. |

### Platforms

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| iPhone and iPad (Safari 26+) | `build.iphone` | true |  | needs you |  | Needs testing on your devices. |
| Desktop browsers (Chrome, Edge, Safari) | `build.desktop` | true |  | needs you |  | Needs testing on your devices. |
| Native app (Windows, macOS) | `build.native` | false |  | needs you |  | A new build target to decide on. |

# Editor preferences

Kept per browser; they change the editor, not the game.

## Appearance

### Theme

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Theme | `ed.theme` | dark | dark / midnight / light | editor |  |  |
| Accent | `ed.accent` | #4c9dff |  | editor |  |  |
| Interface text size | `ed.fontSize` | 12 | 10 … 18 px | editor |  |  |
| Density | `ed.density` | comfortable | compact / comfortable / spacious | editor |  |  |
| Tooltips | `ed.tooltips` | true |  | editor |  |  |
| Show status badges | `ed.badges` | true |  | editor |  |  |

## Layout

### Window

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Layout | `ed.layout` | default | default / scripting / lighting / performance / viewport | editor |  |  |
| Toolbar | `ed.toolbar` | true |  | editor |  |  |
| Status bar | `ed.statusbar` | true |  | editor |  |  |
| Remember panel layout | `ed.rememberLayout` | true |  | editor |  |  |

## Viewport

### Overlays

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Stats overlay | `vp.stats` | true |  | editor |  |  |
| Grid | `vp.grid` | false |  | editor |  |  |
| Rule of thirds | `vp.thirds` | false |  | editor |  |  |
| Safe frames | `vp.safe` | false |  | editor |  |  |
| Centre mark | `vp.crosshair` | false |  | editor |  |  |
| Refresh stats every | `vp.statsEvery` | 500 | 100 … 2000 ms | editor |  |  |

### Navigation

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Look with | `vp.drag` | any | any / right | editor |  |  |
| Keys move only when the viewport has focus | `vp.keysNeedFocus` | true |  | editor |  |  |
| Free camera speed | `vp.flySpeed` | 5 | 0.5 … 50 m/s | not built yet |  | Comes with object editing. |

## Gizmos & snapping

### Snapping

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Snap moves | `snap.move` | true |  | not built yet |  | Comes with object editing. |
| Move step | `snap.moveStep` | 0.25 | 0.01 / 0.05 / 0.1 / 0.25 / 0.5 / 1 / 5 / 10 | not built yet |  | Comes with object editing. |
| Snap turns | `snap.rotate` | true |  | not built yet |  | Comes with object editing. |
| Turn step | `snap.rotateStep` | 15 | 1 / 5 / 10 / 15 / 30 / 45 / 90 | not built yet |  | Comes with object editing. |
| Snap scale | `snap.scale` | false |  | not built yet |  | Comes with object editing. |
| Scale step | `snap.scaleStep` | 0.1 | 0.01 / 0.05 / 0.1 / 0.25 / 0.5 / 1 | not built yet |  | Comes with object editing. |

### Gizmo

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Size | `gizmo.size` | 1 | 0.5 … 3 x | not built yet |  | Comes with object editing. |
| Space | `gizmo.space` | world | world / local | not built yet |  | Comes with object editing. |

## Script editor

### Editing

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Text size | `code.fontSize` | 13 | 10 … 22 px | editor |  |  |
| Tab width | `code.tab` | 2 | 2 / 4 | editor |  |  |
| Wrap long lines | `code.wrap` | false |  | editor |  |  |
| Line numbers | `code.lineNumbers` | true |  | editor |  |  |
| Run on Ctrl+S | `code.runOnSave` | true |  | editor |  |  |
| Keep indentation on Enter | `code.autoIndent` | true |  | editor |  |  |
| Word suggestions | `code.autocomplete` | true |  | editor |  |  |
| Minimap | `code.minimap` | false |  | editor |  |  |

## Console

### Messages

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Keep at most | `con.max` | 1000 | 100 … 10000 lines | editor |  |  |
| Timestamps | `con.time` | true |  | editor |  |  |
| Clear when a scene loads | `con.clearOnLoad` | false |  | editor |  |  |
| Engine messages | `con.engine` | true |  | editor |  |  |
| Dialogue lines | `con.lines` | true |  | editor |  |  |

## Autosave

### Autosave

| Setting | Name | Default | Range | Status | In a script | What it does |
|---|---|---|---|---|---|---|
| Keep script edits in this browser | `save.auto` | true |  | editor |  |  |
| Every | `save.every` | 5 | 1 … 60 s | editor |  |  |

---

10 project.json · 131 live · script · 49 live · 39 not built yet · 37 editor · 6 needs you
