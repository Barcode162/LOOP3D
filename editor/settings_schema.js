// Every setting in the Loop3D editor, in one place. The settings windows,
// search, reset, import/export, the inspector and docs/SETTINGS.md
// (tools/settings_doc.js) are all built from this.
//
// A setting: { id, label, type, def, ...range, help, bind, word }
//   type: toggle | slider | number | enum | color | vec3 | key | text
//   bind: how the engine hears it
//     set              an engine setting of the same name (engine/core/settings.h):
//                      also settable from a script (`setting "id", value`) and project.json
//     quality          the live quality (Phone, High, Ultra)
//     look:<field>     the film look over the scene's (a scene sets it with look(…))
//     world:<field>    fog, sky, sun and camera over the scene's
//     editor           the editor itself acts on it
//     build            tools/build_site.sh and build_projects.py read it from project.json
//     project          project.json's own fields (title, start scene, …)
//   status (no bind): 'nb' a feature not built yet, 'you' needs you (publishing, devices)
//   word: the Looped Script that sets the same thing, for the docs
window.LE = window.LE || {};
(() => {
  const S = (id, label, type, def, extra = {}) => ({ id, label, type, def, ...extra });
  const tog = (id, label, def, extra) => S(id, label, 'toggle', def, extra);
  const sl = (id, label, def, min, max, step, extra) => S(id, label, 'slider', def, { min, max, step, ...extra });
  const num = (id, label, def, extra) => S(id, label, 'number', def, extra);
  const en = (id, label, def, options, extra) => S(id, label, 'enum', def, { options, ...extra });
  const col = (id, label, def, extra) => S(id, label, 'color', def, extra);
  const v3 = (id, label, def, extra) => S(id, label, 'vec3', def, extra);
  const key = (id, label, def, extra) => S(id, label, 'key', def, extra);
  const txt = (id, label, def, extra) => S(id, label, 'text', def, extra);
  const opts = (...pairs) => pairs.map((p) => (Array.isArray(p) ? { value: p[0], label: p[1] } : { value: p, label: String(p) }));
  const E = (extra = {}) => ({ bind: 'set', ...extra });   // an engine setting
  const NB = (help) => ({ status: 'nb', help });            // a feature not built yet
  const YOU = (help) => ({ status: 'you', help });          // needs you

  const looks = ['natural', 'blockbuster', 'noir', 'golden', 'bleach', 'seventies', 'neon', 'dream', 'documentary', 'horror', 'arctic', 'western'];
  LE.LOOKS = looks;
  const pct = (v) => Math.round(v * 100) + '%';

  // ---------------------------------------------------------------- project
  const project = [
    { id: 'general', title: 'General', icon: '◆', groups: [
      { title: 'Project', items: [
        txt('project.title', 'Title', '', { bind: 'project', help: 'Shown in the player\'s scene list and the editor. Saved in project.json.' }),
        txt('project.description', 'Description', '', { bind: 'project', help: 'One or two sentences about the project.' }),
        txt('project.start', 'Start scene', '', { bind: 'project', help: 'The scene the project opens with.' }),
        en('project.mode', 'Mode for scenes that don\'t say', '', opts(['', 'Game (walk around)'], ['movie', 'Movie (plays its shots)']), E({ word: 'mode "movie"' })),
      ] },
      { title: 'Folders', items: [
        txt('project.scenesDir', 'Scenes folder', '.', { bind: 'build', help: 'Relative to the project folder.' }),
        txt('project.stillsDir', 'Trailer stills folder', 'stills', { bind: 'build' }),
        txt('project.libraryDir', 'Shared scripts folder', '.', { bind: 'build', help: 'Where `use "name"` scripts are.' }),
      ] },
    ] },

    { id: 'rendering', title: 'Rendering', icon: '▣', groups: [
      { title: 'Quality', items: [
        en('render.quality', 'Live quality', 1, opts([0, 'Phone'], [1, 'High'], [2, 'Ultra']), { bind: 'quality', help: 'Phone: 1x pixels, sun shadows, lamps without shadows. High: sharper, crisp shadows from the two lamps that matter most. Ultra: path traced on the GPU.' }),
        en('render.targetFps', 'Target frame rate', 60, opts(30, 60, 90, 120, 144), E({ help: 'What the automatic resolution aims for (never above the display\'s own rate).' })),
        en('render.framesInFlight', 'Frames in flight', 2, opts(1, 2, 3), E({ help: 'More hides GPU stalls; fewer means less delay.' })),
        tog('render.pauseHidden', 'Stop drawing when the page is hidden', true, E()),
      ] },
      { title: 'Resolution', items: [
        sl('render.res', 'Hold resolution', 0, 0, 1, 0.05, E({ fmt: (v) => (v <= 0 ? 'Automatic' : pct(v)), help: '0 = automatic (climbs while the GPU keeps up, drops when it doesn\'t). Any other value holds it there, to compare costs fairly.' })),
        sl('render.ratioPhone', 'Pixel density cap: Phone', 1, 0.5, 3, 0.25, E({ unit: 'x' })),
        sl('render.ratioHigh', 'Pixel density cap: High', 1.5, 0.5, 3, 0.25, E({ unit: 'x' })),
        sl('render.ratioUltra', 'Pixel density cap: Ultra', 1, 0.5, 3, 0.25, E({ unit: 'x' })),
        sl('render.floor', 'Lowest automatic resolution', 0.5, 0.35, 1, 0.01, E({ fmt: pct })),
        sl('render.floorUltra', 'Lowest automatic resolution (Ultra)', 0.35, 0.35, 1, 0.01, E({ fmt: pct })),
        sl('render.slowGpu', 'Too slow when the GPU takes', 110, 50, 300, 5, E({ unit: '% of a frame', help: 'Over this share of the frame budget: drop the resolution.' })),
        sl('render.climbUnder', 'Climb if the next size fits under', 100, 30, 150, 5, E({ unit: '% of a frame' })),
        sl('render.calm', 'Steady time before climbing', 3, 0.5, 30, 0.5, E({ unit: 's' })),
        en('render.upscaler', 'Temporal upscaling (TAAU)', 'off', opts('off', 'on'), NB('Render about 65% and rebuild full detail from past frames: the biggest speed-up on the roadmap.')),
      ] },
      { title: 'Anti-aliasing', items: [
        en('render.msaa', 'MSAA', 4, opts([1, 'Off'], [4, '4x']), E({ help: 'Multisampling of the scene pass. Changes live (the pipelines are rebuilt).' })),
        tog('render.fxaa', 'Edge smoothing (FXAA)', false, E({ help: 'Cheap smoothing of hard edges in the film pass; most useful with MSAA off.' })),
        tog('render.taa', 'Temporal anti-aliasing', false, NB('Comes with temporal upscaling.')),
      ] },
      { title: 'Sun shadows', items: [
        tog('render.sunShadows', 'Sun shadows', true, E()),
        en('render.sunMapPhone', 'Shadow map: Phone', 1024, opts(512, 1024, 2048, 4096), E()),
        en('render.sunMapHigh', 'Shadow map: High', 2048, opts(1024, 2048, 4096, 8192), E()),
        sl('render.shadowDistance', 'Shadow area', 0, 0, 300, 5, E({ fmt: (v) => (v <= 0 ? 'Quality default' : v + ' m'), help: 'Half-width of the area with sun shadows. Bigger reaches further but blurs (0 = Phone 25 m, High 40 m).' })),
        sl('render.shadowBias', 'Depth bias', 0.0015, 0, 0.02, 0.0005, E({ help: 'Higher hides speckled "acne", lower keeps shadows touching what casts them.' })),
        sl('render.shadowSoftness', 'Softness', 1.8, 0, 8, 0.1, E({ unit: 'texels' })),
        en('render.cascades', 'Cascades', 1, opts(1, 2, 3, 4), NB('Several shadow maps by distance: sharp near, wide far.')),
        tog('render.cachedShadows', 'Cache still shadows', false, NB('Redraw only what moved.')),
      ] },
      { title: 'Lamp shadows', items: [
        tog('render.lampShadows', 'Lamp shadows', true, E()),
        num('render.lamps', 'Lamps that cast shadows', -1, E({ min: -1, max: 16, step: 1, fmt: (v) => (v < 0 ? 'Quality default' : v), help: '-1 = the quality\'s own (Phone 0, High 2). Each one costs about as much as all other lighting.' })),
        num('render.rays', 'Rays per lamp', -1, E({ min: -1, max: 16, step: 1, fmt: (v) => (v < 0 ? '1' : v), help: 'Soft edges need about 4.' })),
        tog('render.lampHalfRes', 'Half-resolution lamp shadows', false, NB('About a quarter of the cost, rebuilt with an edge-aware upsample.')),
        tog('render.contactShadows', 'Contact shadows', false, NB('Short screen-space rays for small creases.')),
      ] },
      { title: 'Lighting', items: [
        num('render.maxLights', 'Most lamps drawn', 256, E({ min: 1, max: 256, step: 1, help: 'The brightest near the camera are kept.' })),
        sl('render.skyLight', 'Light from the sky', 1, 0, 4, 0.05, E({ unit: 'x' })),
        sl('render.occlusion', 'Shading in creases and undersides', 1, 0, 2, 0.05, E({ unit: 'x' })),
        tog('render.clustered', 'Clustered lights', false, NB('Lamps sorted into screen tiles, so 30+ lamps cost little more than 3.')),
        en('render.ao', 'Screen-space ambient occlusion', 'off', opts('off', 'SSAO', 'GTAO'), NB('Real contact darkening from the depth buffer.')),
        en('render.reflections', 'Reflections', 'sky', opts(['sky', 'Sky only'], ['ssr', 'Screen space']), NB('Screen-space reflections.')),
        en('render.gi', 'Bounced light from probes', 'off', opts('off', 'on'), NB('Light probes for bounced light in the live picture.')),
      ] },
      { title: 'Passes', items: [
        tog('render.passScene', 'Scene lighting', true, E()),
        tog('render.passExposure', 'Exposure', true, E()),
        tog('render.passBloom', 'Bloom', true, E()),
        tog('render.passFilm', 'Film (tone curve, grading, grain)', true, E()),
        tog('render.passShadow', 'Shadow pass', true, E()),
        tog('render.passDraw', 'Scene pass', true, E()),
        tog('render.prepass', 'Depth pre-pass', false, E({ help: 'Shade each pixel once. Measured no gain yet; needs @invariant positions before it\'s safe on Apple GPUs.' })),
      ] },
      { title: 'Debug', items: [
        en('render.view', 'Debug view', 0, opts([0, 'Final picture'], [1, 'Raw light'], [2, 'Exposure'], [3, 'Bloom'], [4, 'Ray work (blue little, red 200+ steps)']), E()),
        tog('render.showLights', 'Light icons', false, { bind: 'editor', help: 'Drawn over the viewport by the editor.' }),
        tog('render.showBounds', 'Bounding boxes', false, { bind: 'editor', help: 'Drawn over the viewport by the editor.' }),
        tog('render.wireframe', 'Wireframe', false, NB('WebGPU has no line mode: needs its own pipeline.')),
      ] },
    ] },

    { id: 'ultra', title: 'Ultra (path tracing)', icon: '✦', groups: [
      { title: 'Sampling', items: [
        sl('ultra.bounces', 'Bounces', 5, 1, 16, 1, E()),
        sl('ultra.spp', 'Paths per pixel per frame', 1, 1, 16, 1, E({ help: 'Cleaner each frame, slower each frame.' })),
        sl('ultra.history', 'Most frames averaged', 0, 0, 1024, 8, E({ fmt: (v) => (v ? v : 'No limit'), help: 'A limit keeps the picture following slow changes; 0 keeps sharpening while still.' })),
        sl('ultra.clamp', 'Firefly clamp', 4, 0.5, 50, 0.5, E({ help: 'Brightest a single path may be (lower: fewer sparkles, a little darker highlights).' })),
        tog('ultra.restir', 'Many-light sampling (ReSTIR)', false, NB('Dozens of lamps with little noise.')),
        tog('ultra.blueNoise', 'Blue-noise sampling', false, NB('Noise that looks like film grain.')),
      ] },
      { title: 'Noise removal', items: [
        tog('ultra.denoise', 'Noise removal', true, E()),
        sl('ultra.denoisePasses', 'Strength', 4, 1, 4, 1, E({ unit: 'passes' })),
        tog('ultra.motionVectors', 'Reprojection while moving (SVGF)', false, NB('A clean picture while the camera moves.')),
      ] },
      { title: 'Features', items: [
        tog('ultra.glossy', 'Glossy reflections', true, E()),
        tog('ultra.glass', 'Glass and refraction', true, E()),
        tog('ultra.dof', 'Depth of field', true, E()),
        tog('ultra.sunShadows', 'Traced sun shadows', true, E()),
        tog('ultra.lampShadows', 'Shadows from every lamp', true, E()),
      ] },
    ] },

    { id: 'cinema', title: 'Cinema renderer', icon: '🎞', groups: [
      { title: 'Stills and films (loop render)', items: [
        en('cinema.quality', 'Quality', '', opts(['', 'Good (the default)'], 'draft', 'good', 'cinema', 'ultra'), E({ word: 'render(quality: "cinema")', help: 'Read by `loop render` from project.json, and by render() and film() in scripts.' })),
        num('cinema.width', 'Width', 0, E({ min: 0, max: 8192, unit: 'px', fmt: (v) => (v ? v + ' px' : 'Default'), help: '0 = render() 1920, film() 1280.' })),
        num('cinema.height', 'Height', 0, E({ min: 0, max: 8192, unit: 'px', fmt: (v) => (v ? v + ' px' : '16:9'), help: '0 = 16:9 of the width.' })),
        num('cinema.samples', 'Samples per pixel', 0, E({ min: 0, max: 65536, fmt: (v) => (v ? v : 'From quality') })),
        sl('cinema.bounces', 'Bounces', 10, 1, 64, 1, E()),
        tog('cinema.denoise', 'Noise removal', true, E()),
        tog('cinema.dispersion', 'Rainbow dispersion in glass', true, E()),
        tog('cinema.motionBlur', 'Motion blur', true, E()),
        num('cinema.fps', 'Film frames per second', 24, E({ min: 1, max: 120, word: 'film(fps: 24)' })),
        txt('cinema.folder', 'Film output folder', 'frames', E()),
        num('cinema.threads', 'Threads', 0, E({ min: 0, max: 256, fmt: (v) => (v ? v : 'All') })),
      ] },
    ] },

    { id: 'film', title: 'Post-process / Film', icon: '◐', groups: [
      { title: 'Look', items: [
        en('look.preset', 'Look', '', opts(['', "Scene's own"], ...looks.map((l) => [l, l[0].toUpperCase() + l.slice(1)])), { bind: 'look:preset', word: 'look "noir"' }),
        en('look.tonemap', 'Tone curve', 'agx', opts(['agx', 'AgX'], ['aces', 'ACES'], ['filmic', 'Filmic'], ['linear', 'Linear']), { bind: 'look:tonemap', word: 'look(tone: "aces")' }),
        sl('look.contrast', 'Contrast', 1, 0, 2, 0.01, { bind: 'look:contrast', word: 'look(contrast: 1.2)' }),
        sl('look.saturation', 'Saturation', 1, 0, 2, 0.01, { bind: 'look:saturation', word: 'look(saturation: 0.8)' }),
        tog('look.monochrome', 'Black and white', false, { bind: 'look:monochrome', word: 'look(monochrome: yes)' }),
      ] },
      { title: 'White balance', items: [
        sl('look.temperature', 'Warmth', 0, -1, 1, 0.01, { bind: 'look:temperature', word: 'look(warmth: 0.3)', help: 'Blue to amber.' }),
        sl('look.tint', 'Tint', 0, -1, 1, 0.01, { bind: 'look:tint', word: 'look(tint: 0.1)', help: 'Green to magenta.' }),
      ] },
      { title: 'Grading', items: [
        v3('look.lift', 'Lift (shadows)', [0, 0, 0], { min: -0.2, max: 0.2, step: 0.005, bind: 'look:lift', word: 'look(lift: vec(0, 0, 0.02))' }),
        v3('look.gamma', 'Gamma (mids)', [1, 1, 1], { min: 0.5, max: 2, step: 0.01, bind: 'look:gamma', word: 'look(gamma: vec(1, 1, 1.05))' }),
        v3('look.gain', 'Gain (highlights)', [1, 1, 1], { min: 0.5, max: 2, step: 0.01, bind: 'look:gain', word: 'look(gain: …)' }),
        v3('look.shadowTint', 'Split tone: shadows', [0, 0, 0], { min: -0.2, max: 0.2, step: 0.005, bind: 'look:shadowTint', word: 'look(shadow_tint: …)' }),
        v3('look.highlightTint', 'Split tone: highlights', [0, 0, 0], { min: -0.2, max: 0.2, step: 0.005, bind: 'look:highlightTint', word: 'look(highlight_tint: …)' }),
        sl('look.blackLevel', 'Faded blacks', 0, 0, 0.2, 0.005, { bind: 'look:blackLevel', word: 'look(black_level: 0.03)' }),
      ] },
      { title: 'Glow', items: [
        sl('look.bloom', 'Bloom', 0.04, 0, 0.5, 0.005, { bind: 'look:bloom', word: 'look(bloom: 0.1)' }),
        sl('look.bloomRadius', 'Bloom size', 0.02, 0.005, 0.08, 0.001, { bind: 'look:bloomRadius', word: 'look(bloom_size: 0.03)' }),
        sl('look.halation', 'Halation (red film glow)', 0, 0, 1, 0.01, { bind: 'look:halation', word: 'look(halation: 0.3)' }),
      ] },
      { title: 'Lens', items: [
        sl('look.vignette', 'Vignette', 0.25, 0, 1, 0.01, { bind: 'look:vignette', word: 'look(vignette: 0.4)' }),
        sl('look.chromaticAberration', 'Colour fringes', 0, 0, 6, 0.1, { bind: 'look:chromaticAberration', unit: 'px', word: 'look(fringes: 1.5)' }),
        sl('look.distortion', 'Distortion', 0, -0.3, 0.3, 0.005, { bind: 'look:distortion', word: 'look(distortion: 0.1)', help: '+ barrel, − pincushion.' }),
        sl('look.sharpen', 'Sharpen', 0, 0, 1, 0.01, { bind: 'look:sharpen', word: 'look(sharpen: 0.3)' }),
      ] },
      { title: 'Film stock', items: [
        sl('look.grain', 'Grain', 0, 0, 0.3, 0.005, { bind: 'look:grain', word: 'look(grain: 0.05)' }),
        sl('look.grainSize', 'Grain size', 1, 0.5, 4, 0.05, { bind: 'look:grainSize', word: 'look(grain_size: 2)' }),
        en('look.letterbox', 'Cinema bars', 0, opts([0, 'None'], [1.85, '1.85 : 1'], [2, '2 : 1'], [2.39, '2.39 : 1 (scope)'], [2.76, '2.76 : 1 (Ultra Panavision)']), { bind: 'look:letterbox', word: 'look(bars: 2.39)' }),
      ] },
    ] },

    { id: 'camera', title: 'Camera & exposure', icon: '◎', groups: [
      { title: 'Exposure', items: [
        tog('cam.autoExposure', 'Automatic exposure', true, { bind: 'world:autoExposure', word: 'camera(auto_exposure: yes)', help: 'Meter the scene, like an eye.' }),
        sl('cam.exposureCompensation', 'Exposure compensation', 0, -5, 5, 0.1, { unit: 'stops', bind: 'world:exposureCompensation', word: 'camera(exposure: 1)' }),
        num('cam.iso', 'ISO', 100, { min: 25, max: 102400, bind: 'world:iso', word: 'camera(iso: 400)' }),
        num('cam.shutter', 'Shutter', 0.0208, { min: 0.0001, max: 1, step: 0.0001, unit: 's', bind: 'world:shutter', word: 'camera(shutter: 1/48)', fmt: (v) => '1/' + Math.round(1 / v) }),
        sl('cam.adaptSpeed', 'Eye adaptation speed', 1, 0.1, 10, 0.1, E({ unit: 'x' })),
        sl('cam.minEv', 'Darkest it adapts to', -12, -16, 10, 0.5, E({ unit: 'EV' })),
        sl('cam.maxEv', 'Brightest it adapts to', 23, 0, 26, 0.5, E({ unit: 'EV' })),
      ] },
      { title: 'Lens', items: [
        sl('cam.focalLength', 'Focal length', 35, 8, 300, 1, { unit: 'mm', bind: 'world:focalLength', word: 'camera(lens: 50 mm)' }),
        sl('cam.fStop', 'Aperture', 2.8, 0.95, 32, 0.05, { unit: 'f/', bind: 'world:fStop', word: 'camera(f_stop: 1.8)' }),
        sl('cam.focusDistance', 'Focus distance', 0, 0, 100, 0.1, { unit: 'm', bind: 'world:focusDistance', word: 'camera(focus: 3 m)', fmt: (v) => (v <= 0 ? 'At target' : v + ' m') }),
        sl('cam.blades', 'Aperture blades', 7, 0, 16, 1, { bind: 'world:blades', word: 'camera(blades: 9)', fmt: (v) => (v === 0 ? 'Round' : v), help: 'Shapes the out-of-focus highlights in cinema renders.' }),
        en('cam.sensor', 'Sensor', 'ff', opts(['ff', 'Full frame 36×24'], ['s35', 'Super 35'], ['apsc', 'APS-C'], ['m43', 'Micro 4/3'], ['imax', 'IMAX 65']), { bind: 'world:sensor', word: 'camera(sensor: "s35")' }),
      ] },
      { title: 'Motion', items: [
        sl('cam.shake', 'Handheld shake (walking)', 0, 0, 1, 0.01, E()),
        sl('cam.smoothing', 'Look smoothing', 18, 1, 60, 1, E({ help: 'How quickly the view eases to where you point it (higher = snappier).' })),
        tog('cam.motionBlur', 'Live motion blur', false, NB('Needs motion vectors.')),
      ] },
    ] },

    { id: 'sky', title: 'Sky, sun & fog', icon: '☀', groups: [
      { title: 'Fog', items: [
        sl('fog.density', 'Density', 0, 0, 0.2, 0.001, { bind: 'world:fogDensity', word: 'fog(0.02)', help: 'Per metre. 0.01 is a light haze.' }),
        v3('fog.albedo', 'Colour', [0.9, 0.9, 0.9], { min: 0, max: 1, step: 0.01, bind: 'world:fogAlbedo', word: 'fog(color: …)' }),
        sl('fog.anisotropy', 'God rays (forward scattering)', 0.6, -0.9, 0.95, 0.01, { bind: 'world:fogAnisotropy', word: 'fog(glow: 0.8)' }),
        sl('fog.top', 'Fog top', 1000000, 0, 1000000, 1, { unit: 'm', bind: 'world:fogTop', word: 'fog(top: 8 m)', fmt: (v) => (v >= 1e6 ? 'No limit' : v + ' m') }),
      ] },
      { title: 'Sky', items: [
        en('sky.kind', 'Sky', 'physical', opts(['physical', 'Physical (Rayleigh + Mie + ozone)'], ['color', 'Flat colour'], ['none', 'None']), { bind: 'world:skyKind', word: 'sky "physical"' }),
        sl('sky.turbidity', 'Haze', 2.5, 1.7, 10, 0.1, { bind: 'world:skyTurbidity', word: 'sky(haze: 4)', help: '2 clear … 10 smoggy.' }),
        sl('sky.intensity', 'Brightness', 1, 0, 4, 0.05, { bind: 'world:skyIntensity', word: 'sky(brightness: 1.5)' }),
        sl('sky.stars', 'Stars', 0, 0, 1, 0.01, { bind: 'world:skyStars', word: 'sky(stars: 0.02)' }),
        col('sky.color', 'Flat sky colour', '#6699ff', { bind: 'world:skyColor', word: 'sky("color", color: …)' }),
        v3('sky.ground', 'Ground colour', [0.3, 0.3, 0.3], { min: 0, max: 1, step: 0.01, bind: 'world:skyGround', word: 'sky(ground: vec(0.2, 0.25, 0.15))' }),
      ] },
      { title: 'Sun', items: [
        sl('sun.elevation', 'Elevation', 45, -10, 90, 0.5, { unit: '°', bind: 'world:sunElevation', word: 'sun(elevation: 30 deg)' }),
        sl('sun.azimuth', 'Toward', 180, 0, 360, 1, { unit: '°', bind: 'world:sunAzimuth', word: 'sun(toward: 135 deg)' }),
        num('sun.lux', 'Brightness', 100000, { min: 0, max: 150000, unit: 'lux', bind: 'world:sunLux', word: 'sun(lux: 80000)' }),
        sl('sun.size', 'Size in the sky', 0.53, 0.1, 10, 0.01, { unit: '°', bind: 'world:sunSize', word: 'sun(size: 1 deg)', help: 'Wider: softer shadows. Our sun is 0.53°.' }),
        tog('sun.dayCycle', 'Day cycle', false, { bind: 'world:dayCycle', help: 'The sun goes round on its own.' }),
        sl('sun.daySpeed', 'Day length', 24, 0.5, 1440, 0.5, { unit: 'min', bind: 'world:daySpeed' }),
      ] },
    ] },

    { id: 'world', title: 'World', icon: '⬢', groups: [
      { title: 'Distance and detail', items: [
        sl('world.drawDistance', 'Draw distance', 4000, 50, 20000, 50, E({ unit: 'm' })),
        sl('world.patternFade', 'Pattern detail fade', 1, 0.25, 4, 0.05, E({ unit: 'x', help: 'Higher fades fine patterns (bricks, grain) sooner with distance: less shimmer.' })),
        sl('world.foliageDensity', 'Forest density', 1, 0, 3, 0.05, E({ unit: 'x', word: 'forest(spacing: …)', help: 'Applies when a scene builds (Run).' })),
        sl('world.terrainRes', 'Terrain detail', 1, 0.25, 4, 0.05, E({ unit: 'x', word: 'terrain(detail: …)', help: 'Applies when a scene builds (Run).' })),
      ] },
      { title: 'Big worlds', items: [
        tog('world.streaming', 'Stream big worlds', false, NB('Load and unload cells as you move.')),
        tog('world.gpuCulling', 'GPU culling', false, NB('Skip what\'s off screen or hidden.')),
        tog('world.impostors', 'Flat cut-outs far away', false, NB('Trees and people as cards in the distance.')),
        sl('world.lodBias', 'Level-of-detail bias', 0, -2, 2, 0.1, NB('Needs levels of detail first.')),
      ] },
    ] },

    { id: 'physics', title: 'Physics', icon: '⚛', groups: [
      { title: 'Simulation', items: [
        v3('physics.gravity', 'Gravity', [0, -9.81, 0], E({ min: -100, max: 100, step: 0.01, unit: 'm/s²', word: 'gravity(vec(0, -1.62, 0))' })),
        en('physics.rate', 'Steps per second', 120, opts(60, 120, 240), E()),
        sl('physics.iterations', 'Solver iterations', 16, 1, 64, 1, E({ help: 'More: stiffer stacks, slower.' })),
        sl('physics.sleep', 'Sleep below', 0.05, 0, 1, 0.01, E({ unit: 'm/s' })),
        tog('physics.ccd', 'Continuous collisions', false, NB('So fast things never pass through thin walls.')),
      ] },
      { title: 'Defaults', items: [
        sl('physics.friction', 'Friction', 0.6, 0, 2, 0.01, E({ word: 'solid(ball, friction: 0.3)' })),
        sl('physics.bounce', 'Bounce', 0.2, 0, 1, 0.01, E({ word: 'solid(ball, bounce: 0.7)' })),
      ] },
      { title: 'Walking body', items: [
        sl('physics.charRadius', 'Radius', 0.25, 0.1, 1, 0.01, E({ unit: 'm' })),
        sl('physics.charHeight', 'Height', 1.1, 0.4, 2.5, 0.01, E({ unit: 'm' })),
        sl('physics.charStep', 'Step height', 0.3, 0, 1, 0.01, E({ unit: 'm' })),
        sl('physics.charSlope', 'Steepest walkable', 53, 10, 85, 1, E({ unit: '°' })),
      ] },
      { title: 'Cloth', items: [
        sl('physics.clothIterations', 'Cloth iterations', 8, 1, 64, 1, E()),
        v3('physics.wind', 'Wind', [0, 0, 0], E({ min: -50, max: 50, step: 0.1, unit: 'm/s' })),
      ] },
    ] },

    { id: 'player', title: 'Player & controls', icon: '⌖', groups: [
      { title: 'Movement', items: [
        sl('player.walk', 'Walk speed', 1.6, 0.2, 10, 0.1, E({ unit: 'm/s' })),
        sl('player.run', 'Run speed', 4.5, 0.5, 20, 0.1, E({ unit: 'm/s' })),
        sl('player.eye', 'Eye height', 1.6, 0.3, 3, 0.01, E({ unit: 'm' })),
        sl('player.bob', 'Head bob', 0.025, 0, 0.1, 0.001, E({ unit: 'm' })),
        sl('player.accel', 'Start and stop', 7, 1, 30, 0.5, E()),
      ] },
      { title: 'Looking', items: [
        sl('player.look', 'Mouse sensitivity', 1, 0.1, 5, 0.05, E({ unit: 'x' })),
        tog('player.invertY', 'Invert vertical look', false, E()),
        sl('player.touchLook', 'Touch look speed', 1.6, 0.2, 5, 0.1, E({ unit: 'x' })),
        sl('player.stickSize', 'Thumb stick size', 110, 60, 200, 5, E({ unit: 'px' })),
        sl('player.deadzone', 'Gamepad dead zone', 0.15, 0, 0.5, 0.01, E()),
      ] },
      { title: 'Key bindings', items: [
        key('keys.forward', 'Forward', 'KeyW', E()),
        key('keys.back', 'Back', 'KeyS', E()),
        key('keys.left', 'Left', 'KeyA', E()),
        key('keys.right', 'Right', 'KeyD', E()),
        key('keys.run', 'Run', 'ShiftLeft', E()),
        key('keys.use', 'Use / talk', 'KeyE', E()),
      ] },
    ] },

    { id: 'animation', title: 'Animation & people', icon: '☺', groups: [
      { title: 'Bodies', items: [
        tog('anim.ik', 'Feet planted with IK', true, E()),
        tog('anim.breathing', 'Breathing', true, E()),
        tog('anim.armSwing', 'Arm swing', true, E()),
        sl('anim.blend', 'Time to change speed', 0.17, 0.02, 2, 0.01, E({ unit: 's' })),
        num('anim.maxPeople', 'Most people in a scene', 200, E({ min: 1, max: 5000 })),
        tog('anim.skinned', 'Skinned bodies', false, NB('Smooth bodies instead of capsules.')),
        tog('anim.faces', 'Faces', false, NB()),
        tog('anim.hair', 'Hair', false, NB()),
      ] },
      { title: 'Crowds', items: [
        tog('anim.steering', 'Walk around obstacles', false, NB('People now walk straight to where they\'re sent.')),
        sl('anim.crowdLod', 'Simple bodies beyond', 30, 5, 200, 1, NB('Needs levels of detail.')),
      ] },
    ] },

    { id: 'audio', title: 'Audio', icon: '♪', groups: [
      { title: 'Volume', items: [
        sl('audio.master', 'Master', 1, 0, 2, 0.01, E()),
        sl('audio.ambience', 'Ambience (fire, wind, rain…)', 1, 0, 2, 0.01, E()),
        sl('audio.effects', 'Effects and knocks', 1, 0, 2, 0.01, E()),
        sl('audio.footsteps', 'Footsteps', 1, 0, 2, 0.01, E()),
        sl('audio.dialogue', 'Dialogue', 1, 0, 2, 0.01, E({ help: 'For voiced lines (none yet).' })),
        sl('audio.music', 'Music', 1, 0, 2, 0.01, E({ help: 'For music tracks (none yet).' })),
      ] },
      { title: 'Space', items: [
        sl('audio.reverb', 'Room echo', 1, 0, 3, 0.01, E({ unit: 'x', word: 'room(size: 0.6, echo: 0.3)' })),
        sl('audio.distance', 'Distance fall-off', 1, 0.1, 5, 0.05, E({ unit: 'x' })),
        tog('audio.earDelay', 'Ear delay (3D)', true, E()),
        tog('audio.airAbsorb', 'Air absorption', true, E()),
        tog('audio.doppler', 'Doppler', false, E()),
        tog('audio.limiter', 'Limiter', true, E()),
      ] },
      { title: 'Output', items: [
        en('audio.buffer', 'Buffer', 2048, opts(256, 512, 1024, 2048, 4096), E({ help: 'Smaller is less delay but more risk of crackles. Applies when sound is turned on.' })),
        tog('audio.worklet', 'Mix on its own thread (AudioWorklet)', false, NB('Removes a source of hitches.')),
      ] },
    ] },

    { id: 'scripting', title: 'Scripting', icon: '{}', groups: [
      { title: 'Clock', items: [
        en('script.tick', 'Script steps per second', 120, opts(60, 120, 240), E()),
        sl('script.catchUp', 'Most catch-up per frame', 8, 1, 50, 1, E({ unit: 'ms' })),
        sl('script.maxSteps', 'Most steps per frame', 12, 1, 60, 1, E()),
        sl('script.timeScale', 'World speed', 1, 0, 4, 0.05, E({ unit: 'x', help: 'Slow motion and fast forward (walking keeps real time).' })),
      ] },
      { title: 'Problems', items: [
        en('script.onError', 'On a problem', 'continue', opts(['continue', 'Keep running'], ['stop', 'Stop the script'], ['pause', 'Pause the world']), E()),
        tog('script.strict', 'Warn about unused names', false, NB()),
      ] },
      { title: 'Testing', items: [
        tog('script.autoplay', 'Autoplay (the player counts as near everything)', false, E()),
        txt('script.choices', 'Choices picked automatically', '0', E({ help: 'Comma list, used when Autoplay is on (like loop play --choices).' })),
      ] },
    ] },

    { id: 'story', title: 'Story & UI', icon: '❝', groups: [
      { title: 'Dialogue', items: [
        sl('story.textSpeed', 'Reading speed', 15, 3, 60, 1, E({ unit: 'chars/s', word: 'line(…, seconds: 4)' })),
        sl('story.lineMin', 'Shortest line', 2, 0.5, 20, 0.1, E({ unit: 's' })),
        sl('story.subtitleSize', 'Subtitle size', 1, 0.5, 3, 0.05, E({ unit: 'x' })),
        tog('story.speakerNames', 'Show speaker names', true, E()),
        col('story.speakerColor', 'Speaker colour', '#ffd27a', E()),
      ] },
      { title: 'Choices', items: [
        sl('story.choiceTimeout', 'Time to choose', 0, 0, 60, 1, E({ unit: 's', fmt: (v) => (v ? v + ' s' : 'Wait') })),
        en('story.timeoutPick', 'When time runs out', 0, opts([0, 'First option'], [1, 'Second option'], [-1, 'Random']), E()),
      ] },
      { title: 'Between scenes', items: [
        sl('story.fade', 'Fade', 0.6, 0, 5, 0.1, E({ unit: 's' })),
        txt('story.loadingText', 'Loading text', 'Loading {scene}…', E({ help: '{scene} becomes the scene\'s title.' })),
        tog('story.hud', 'Show the buttons and stats (player)', true, E()),
      ] },
    ] },

    { id: 'performance', title: 'Performance & benchmark', icon: '⏱', groups: [
      { title: 'Benchmark', items: [
        sl('perf.benchSeconds', 'Seconds per scene', 20, 5, 120, 1, { unit: 's', bind: 'editor' }),
        sl('perf.hitch', 'A hitch is a frame over', 25, 10, 100, 1, E({ unit: 'ms', help: 'Or twice the median frame, whichever is longer.' })),
        txt('perf.tracker', 'Smoothness Tracker link', '', { bind: 'editor', help: 'Opened by the benchmark results.' }),
      ] },
      { title: 'Threads', items: [
        tog('perf.loadWorker', 'Build scenes off the main thread', false, NB()),
        tog('perf.asyncPipelines', 'Compile GPU pipelines in the background', false, NB()),
        tog('perf.wasmThreads', 'WASM threads and SIMD', false, NB('GitHub Pages needs a service-worker trick for threads.')),
      ] },
    ] },

    { id: 'build', title: 'Build & publish', icon: '⇪', groups: [
      { title: 'Web build', items: [
        tog('build.publish', 'Include this project on the site', true, { bind: 'build' }),
        tog('build.stills', 'Include trailer stills', true, { bind: 'build' }),
        tog('build.stamp', 'Stamp the build on the pages', true, { bind: 'build' }),
        en('build.opt', 'Optimisation', 'O3', opts('O0', 'O1', 'O2', 'O3', 'Os', 'Oz'), { bind: 'build', help: 'Read by tools/build_site.sh (LOOP_OPT).' }),
      ] },
      { title: 'Publishing', items: [
        en('build.target', 'Publish to', 'pages', opts(['pages', 'GitHub Pages'], ['zip', 'A zip file']), YOU('Where it goes, and when, is your call.')),
        tog('build.compress', 'Pre-compress (brotli)', false, YOU('Needs a host that serves .br files.')),
        txt('build.emsdk', 'Emscripten version', '6.0.13', YOU('Set in the Pages workflow.')),
      ] },
      { title: 'Platforms', items: [
        tog('build.iphone', 'iPhone and iPad (Safari 26+)', true, YOU('Needs testing on your devices.')),
        tog('build.desktop', 'Desktop browsers (Chrome, Edge, Safari)', true, YOU('Needs testing on your devices.')),
        tog('build.native', 'Native app (Windows, macOS)', false, YOU('A new build target to decide on.')),
      ] },
    ] },
  ];

  // ---------------------------------------------------------------- editor
  const ED = { bind: 'editor' };
  const editor = [
    { id: 'appearance', title: 'Appearance', icon: '◑', groups: [
      { title: 'Theme', items: [
        en('ed.theme', 'Theme', 'dark', opts(['dark', 'Dark'], ['midnight', 'Midnight'], ['light', 'Light']), ED),
        col('ed.accent', 'Accent', '#4c9dff', ED),
        sl('ed.fontSize', 'Interface text size', 12, 10, 18, 1, { unit: 'px', ...ED }),
        en('ed.density', 'Density', 'comfortable', opts('compact', 'comfortable', 'spacious'), ED),
        tog('ed.tooltips', 'Tooltips', true, ED),
        tog('ed.badges', 'Show status badges', true, ED),
      ] },
    ] },
    { id: 'layout', title: 'Layout', icon: '▤', groups: [
      { title: 'Window', items: [
        en('ed.layout', 'Layout', 'default', opts(['default', 'Default'], ['scripting', 'Scripting'], ['lighting', 'Look & lighting'], ['performance', 'Performance'], ['viewport', 'Viewport only']), ED),
        tog('ed.toolbar', 'Toolbar', true, ED),
        tog('ed.statusbar', 'Status bar', true, ED),
        tog('ed.rememberLayout', 'Remember panel layout', true, ED),
      ] },
    ] },
    { id: 'viewport', title: 'Viewport', icon: '▢', groups: [
      { title: 'Overlays', items: [
        tog('vp.stats', 'Stats overlay', true, ED),
        tog('vp.grid', 'Grid', false, ED),
        tog('vp.thirds', 'Rule of thirds', false, ED),
        tog('vp.safe', 'Safe frames', false, ED),
        tog('vp.crosshair', 'Centre mark', false, ED),
        sl('vp.statsEvery', 'Refresh stats every', 500, 100, 2000, 50, { unit: 'ms', ...ED }),
      ] },
      { title: 'Navigation', items: [
        en('vp.drag', 'Look with', 'any', opts(['any', 'Any mouse button'], ['right', 'Right button (Unreal style)']), ED),
        tog('vp.keysNeedFocus', 'Keys move only when the viewport has focus', true, ED),
        sl('vp.flySpeed', 'Free camera speed', 5, 0.5, 50, 0.5, { unit: 'm/s', ...NB('Comes with object editing.') }),
      ] },
    ] },
    { id: 'gizmos', title: 'Gizmos & snapping', icon: '✣', groups: [
      { title: 'Snapping', items: [
        tog('snap.move', 'Snap moves', true, NB('Comes with object editing.')),
        en('snap.moveStep', 'Move step', 0.25, opts(0.01, 0.05, 0.1, 0.25, 0.5, 1, 5, 10), { unit: 'm', ...NB('Comes with object editing.') }),
        tog('snap.rotate', 'Snap turns', true, NB('Comes with object editing.')),
        en('snap.rotateStep', 'Turn step', 15, opts(1, 5, 10, 15, 30, 45, 90), { unit: '°', ...NB('Comes with object editing.') }),
        tog('snap.scale', 'Snap scale', false, NB('Comes with object editing.')),
        en('snap.scaleStep', 'Scale step', 0.1, opts(0.01, 0.05, 0.1, 0.25, 0.5, 1), NB('Comes with object editing.')),
      ] },
      { title: 'Gizmo', items: [
        sl('gizmo.size', 'Size', 1, 0.5, 3, 0.1, { unit: 'x', ...NB('Comes with object editing.') }),
        en('gizmo.space', 'Space', 'world', opts('world', 'local'), NB('Comes with object editing.')),
      ] },
    ] },
    { id: 'code', title: 'Script editor', icon: '✎', groups: [
      { title: 'Editing', items: [
        sl('code.fontSize', 'Text size', 13, 10, 22, 1, { unit: 'px', ...ED }),
        en('code.tab', 'Tab width', 2, opts(2, 4), ED),
        tog('code.wrap', 'Wrap long lines', false, ED),
        tog('code.lineNumbers', 'Line numbers', true, ED),
        tog('code.runOnSave', 'Run on Ctrl+S', true, ED),
        tog('code.autoIndent', 'Keep indentation on Enter', true, ED),
        tog('code.autocomplete', 'Word suggestions', true, ED),
        tog('code.minimap', 'Minimap', false, ED),
      ] },
    ] },
    { id: 'console', title: 'Console', icon: '›_', groups: [
      { title: 'Messages', items: [
        sl('con.max', 'Keep at most', 1000, 100, 10000, 100, { unit: 'lines', ...ED }),
        tog('con.time', 'Timestamps', true, ED),
        tog('con.clearOnLoad', 'Clear when a scene loads', false, ED),
        tog('con.engine', 'Engine messages', true, ED),
        tog('con.lines', 'Dialogue lines', true, ED),
      ] },
    ] },
    { id: 'autosave', title: 'Autosave', icon: '⟳', groups: [
      { title: 'Autosave', items: [
        tog('save.auto', 'Keep script edits in this browser', true, ED),
        sl('save.every', 'Every', 5, 1, 60, 1, { unit: 's', ...ED }),
      ] },
    ] },
  ];

  LE.SCHEMA = { project, editor };
})();
