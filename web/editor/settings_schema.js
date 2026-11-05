// Every setting in the Loop3D editor, in one place. The settings windows,
// search, reset, import/export and the inspector are all built from this.
//
// A setting: { id, label, type, def, ...range, help, bind }
//   type: toggle | slider | number | enum | color | vec3 | key | text
//   bind: how the engine hears it. Without one, the setting is "Not wired":
//     quality          loop_set_quality
//     opt:<name>       loop_set_option(name, value)
//     skip:<bit>       a pass on/off (loop_set_option('skip', bits))
//     look:<field>     the film look over the scene's (loop_set_look)
//     world:<field>    fog and camera over the scene's (loop_set_world)
//     restart:<param>  a URL switch: applies when the viewport restarts
//     editor           the editor itself acts on it
//     autoplay         loop_set_autoplay
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

  const looks = ['natural', 'blockbuster', 'noir', 'golden', 'bleach', 'seventies', 'neon', 'dream', 'documentary', 'horror', 'arctic', 'western'];
  LE.LOOKS = looks;

  // ---------------------------------------------------------------- project
  const project = [
    { id: 'general', title: 'General', icon: '◆', groups: [
      { title: 'Project', items: [
        txt('project.title', 'Title', '', { help: 'Shown in the player and the editor title bar.' }),
        txt('project.description', 'Description', '', { help: 'One or two sentences about the project.' }),
        txt('project.author', 'Studio', 'J Production'),
        txt('project.version', 'Version', '0.1.0'),
        txt('project.start', 'Start scene', '', { help: 'The scene a build opens with.' }),
        en('project.mode', 'Default mode', 'game', opts(['game', 'Game (walk around)'], ['movie', 'Movie (plays its shots)'])),
      ] },
      { title: 'Folders', items: [
        txt('project.scenesDir', 'Scenes folder', '.', { help: 'Relative to the project folder.' }),
        txt('project.stillsDir', 'Trailer stills folder', 'stills'),
        txt('project.libraryDir', 'Shared scripts', '.', { help: 'Where `use "name"` looks.' }),
      ] },
    ] },

    { id: 'rendering', title: 'Rendering', icon: '▣', groups: [
      { title: 'Quality', items: [
        en('render.quality', 'Live quality', 1, opts([0, 'Phone'], [1, 'High'], [2, 'Ultra']), { bind: 'quality', help: 'Phone: 1x pixels, sun shadows, lamps without shadows. High: 1.5x, crisp shadows from the two lamps that matter most. Ultra: path traced on the GPU.' }),
        tog('render.autoStepDown', 'Step down on GPU loss', true, { help: 'Lost the GPU twice in a minute: drop one quality. Built into the player.', bind: 'editor', always: true }),
        en('render.targetFps', 'Target frame rate', 60, opts(30, 60, 90, 120)),
        en('render.framesInFlight', 'Frames in flight', 2, opts(1, 2, 3), { help: 'More hides GPU stalls; fewer means less delay.' }),
        tog('render.vsync', 'Wait for the display (vsync)', true),
        tog('render.pauseHidden', 'Stop drawing when the tab is hidden', true),
      ] },
      { title: 'Resolution', items: [
        sl('render.res', 'Hold resolution', 0, 0, 1, 0.05, { bind: 'opt:res', fmt: (v) => (v <= 0 ? 'Automatic' : Math.round(v * 100) + '%'), help: '0 = automatic (climbs while the GPU keeps up, drops when it doesn\'t). Any other value holds it there, to compare costs fairly.' }),
        sl('render.ratioPhone', 'Pixel density cap: Phone', 1, 0.5, 3, 0.25, { unit: 'x' }),
        sl('render.ratioHigh', 'Pixel density cap: High', 1.5, 0.5, 3, 0.25, { unit: 'x' }),
        sl('render.ratioUltra', 'Pixel density cap: Ultra', 1, 0.5, 3, 0.25, { unit: 'x' }),
        sl('render.floor', 'Lowest automatic resolution', 0.5, 0.2, 1, 0.05, { fmt: (v) => Math.round(v * 100) + '%' }),
        sl('render.floorUltra', 'Lowest automatic resolution (Ultra)', 0.34, 0.2, 1, 0.02, { fmt: (v) => Math.round(v * 100) + '%' }),
        sl('render.governorUp', 'Climb when GPU time is under', 70, 30, 100, 1, { unit: '% of budget' }),
        sl('render.governorDown', 'Drop when GPU time is over', 95, 50, 150, 1, { unit: '% of budget' }),
        sl('render.governorStep', 'Resolution step', 0.05, 0.01, 0.2, 0.01),
        en('render.upscaler', 'Upscaler', 'stretch', opts(['stretch', 'Bilinear stretch'], ['taau', 'Temporal upscaling (TAAU)'], ['fsr', 'Edge-adaptive (FSR 1 style)']), { help: 'Temporal upscaling is the biggest single speed-up on the roadmap.' }),
      ] },
      { title: 'Anti-aliasing', items: [
        en('render.msaa', 'MSAA', 4, opts([1, 'Off'], [4, '4x']), { bind: 'restart:msaa', help: 'Multisampling of the scene pass. Applies when the viewport restarts.' }),
        en('render.taa', 'Temporal anti-aliasing', 'off', opts('off', 'low', 'medium', 'high')),
        tog('render.fxaa', 'FXAA', false),
        sl('render.taaSharpen', 'TAA sharpening', 0.2, 0, 1, 0.05),
      ] },
      { title: 'Sun shadows', items: [
        tog('render.sunShadows', 'Sun shadows', true, { bind: 'skip:1', help: 'Turning it off skips the shadow pass.' }),
        en('render.sunMapPhone', 'Shadow map: Phone', 1024, opts(512, 1024, 2048, 4096)),
        en('render.sunMapHigh', 'Shadow map: High', 2048, opts(1024, 2048, 4096, 8192)),
        en('render.cascades', 'Cascades', 1, opts(1, 2, 3, 4)),
        sl('render.shadowDistance', 'Shadow distance', 60, 10, 500, 5, { unit: 'm' }),
        sl('render.shadowBias', 'Depth bias', 0.002, 0, 0.02, 0.0005),
        sl('render.shadowSoftness', 'Softness (PCF radius)', 1.5, 0, 6, 0.25),
        tog('render.cachedShadows', 'Cache still shadows', false, { help: 'Redraw only what moved. On the roadmap.' }),
      ] },
      { title: 'Lamp shadows', items: [
        num('render.lamps', 'Lamps that cast shadows', -1, { min: -1, max: 16, step: 1, bind: 'opt:lamps', fmt: (v) => (v < 0 ? 'Quality default' : v), help: '-1 = the quality\'s own choice (Phone 0, High 2). Each one costs about as much as all other lighting.' }),
        num('render.rays', 'Rays per lamp', -1, { min: -1, max: 16, step: 1, bind: 'opt:rays', fmt: (v) => (v < 0 ? 'Quality default' : v), help: 'Soft edges need about 4.' }),
        tog('render.lampShadows', 'Lamp shadows', true, { bind: 'skip:512' }),
        tog('render.lampHalfRes', 'Half-resolution lamp shadows', false, { help: 'About a quarter of the cost, rebuilt with an edge-aware upsample.' }),
        tog('render.contactShadows', 'Contact shadows', false),
      ] },
      { title: 'Lighting', items: [
        num('render.maxLights', 'Most lamps per scene', 64, { min: 1, max: 1024 }),
        tog('render.clustered', 'Clustered lights', false, { help: 'Sort lamps into screen tiles so 30+ lamps cost little more than 3.' }),
        en('render.gi', 'Bounced light (live)', 'sky', opts(['none', 'None'], ['sky', 'Sky lighting'], ['probes', 'Light probes'], ['ultra', 'Path traced (Ultra)'])),
        en('render.ao', 'Ambient occlusion', 'off', opts('off', 'SSAO', 'GTAO', 'ray traced')),
        sl('render.aoRadius', 'AO radius', 0.5, 0.05, 3, 0.05, { unit: 'm' }),
        sl('render.aoStrength', 'AO strength', 1, 0, 2, 0.05),
        en('render.reflections', 'Reflections', 'sky', opts(['sky', 'Sky only'], ['ssr', 'Screen space'], ['rt', 'Ray traced'])),
      ] },
      { title: 'Passes', items: [
        tog('render.passScene', 'Scene lighting', true, { bind: 'skip:2' }),
        tog('render.passExposure', 'Exposure', true, { bind: 'skip:4' }),
        tog('render.passBloom', 'Bloom', true, { bind: 'skip:8' }),
        tog('render.passFilm', 'Film (post-processing)', true, { bind: 'skip:16' }),
        tog('render.passShadow', 'Shadow pass', true, { bind: 'skip:32' }),
        tog('render.passDraw', 'Scene pass', true, { bind: 'skip:64' }),
        tog('render.passDepth', 'Depth buffer', true, { bind: 'skip:128' }),
        tog('render.prepass', 'Depth pre-pass', false, { bind: 'opt:prepass', help: 'Shade each pixel once. Measured no gain yet; needs @invariant positions before it\'s safe on Apple GPUs.' }),
      ] },
      { title: 'Debug', items: [
        en('render.view', 'Debug view', 0, opts([0, 'Final picture'], [1, 'Raw light'], [2, 'Exposure'], [3, 'Bloom'], [4, 'Ray work (blue little, red 200+ steps)']), { bind: 'opt:view' }),
        tog('render.wireframe', 'Wireframe', false),
        tog('render.showBounds', 'Bounding boxes', false),
        tog('render.showLights', 'Light icons', false),
        tog('render.freezeCulling', 'Freeze culling', false),
      ] },
    ] },

    { id: 'ultra', title: 'Ultra (path tracing)', icon: '✦', groups: [
      { title: 'Sampling', items: [
        sl('ultra.bounces', 'Bounces', 3, 1, 12, 1),
        sl('ultra.spp', 'Samples per pixel per frame', 1, 1, 16, 1),
        sl('ultra.history', 'History (frames)', 64, 1, 1024, 1, { help: 'How many frames build up while the camera holds still.' }),
        sl('ultra.clamp', 'Firefly clamp', 10, 1, 100, 1),
        en('ultra.sampler', 'Sampler', 'random', opts(['random', 'Random'], ['blue', 'Blue noise'], ['sobol', 'Sobol'])),
        tog('ultra.restir', 'Many-light sampling (ReSTIR)', false),
      ] },
      { title: 'Denoising', items: [
        en('ultra.denoiser', 'Denoiser', 'temporal', opts(['none', 'None'], ['temporal', 'Temporal'], ['svgf', 'SVGF (reprojection)'])),
        sl('ultra.denoiseStrength', 'Strength', 0.7, 0, 1, 0.05),
        tog('ultra.motionVectors', 'Motion vectors', false),
      ] },
      { title: 'Features', items: [
        tog('ultra.glossy', 'Glossy reflections', true),
        tog('ultra.glass', 'Glass and refraction', true),
        tog('ultra.dof', 'Depth of field', true),
        tog('ultra.sunShadows', 'Traced sun shadows', true),
        tog('ultra.allLamps', 'Shadows from every lamp', true),
      ] },
    ] },

    { id: 'cinema', title: 'Cinema renderer', icon: '🎞', groups: [
      { title: 'Stills and films (loop render)', items: [
        en('cinema.quality', 'Quality', 'cinema', opts('draft', 'good', 'cinema', 'ultra')),
        num('cinema.width', 'Width', 1920, { min: 64, max: 8192, unit: 'px' }),
        num('cinema.height', 'Height', 1080, { min: 64, max: 8192, unit: 'px' }),
        num('cinema.samples', 'Samples per pixel', 256, { min: 1, max: 65536 }),
        sl('cinema.bounces', 'Bounces', 8, 1, 64, 1),
        tog('cinema.denoise', 'Noise removal', true),
        tog('cinema.dispersion', 'Rainbow dispersion in glass', true),
        tog('cinema.motionBlur', 'Motion blur', true),
        num('cinema.fps', 'Film frames per second', 24, { min: 1, max: 120 }),
        txt('cinema.folder', 'Film output folder', 'frames'),
        en('cinema.threads', 'Threads', 'auto', opts('auto', 1, 2, 4, 8, 16, 32)),
      ] },
    ] },

    { id: 'film', title: 'Post-process / Film', icon: '◐', groups: [
      { title: 'Look', items: [
        en('look.preset', 'Look', '', opts(['', "Scene's own"], ...looks.map((l) => [l, l[0].toUpperCase() + l.slice(1)])), { bind: 'look:preset' }),
        en('look.tonemap', 'Tone curve', 'agx', opts(['agx', 'AgX'], ['aces', 'ACES'], ['filmic', 'Filmic'], ['linear', 'Linear']), { bind: 'look:tonemap' }),
        sl('look.contrast', 'Contrast', 1, 0, 2, 0.01, { bind: 'look:contrast' }),
        sl('look.saturation', 'Saturation', 1, 0, 2, 0.01, { bind: 'look:saturation' }),
        tog('look.monochrome', 'Black and white', false, { bind: 'look:monochrome' }),
      ] },
      { title: 'White balance', items: [
        sl('look.temperature', 'Warmth', 0, -1, 1, 0.01, { bind: 'look:temperature', help: 'Blue to amber.' }),
        sl('look.tint', 'Tint', 0, -1, 1, 0.01, { bind: 'look:tint', help: 'Green to magenta.' }),
      ] },
      { title: 'Grading', items: [
        v3('look.lift', 'Lift (shadows)', [0, 0, 0], { min: -0.2, max: 0.2, step: 0.005, bind: 'look:lift' }),
        v3('look.gamma', 'Gamma (mids)', [1, 1, 1], { min: 0.5, max: 2, step: 0.01, bind: 'look:gamma' }),
        v3('look.gain', 'Gain (highlights)', [1, 1, 1], { min: 0.5, max: 2, step: 0.01, bind: 'look:gain' }),
        v3('look.shadowTint', 'Split tone: shadows', [0, 0, 0], { min: -0.2, max: 0.2, step: 0.005, bind: 'look:shadowTint' }),
        v3('look.highlightTint', 'Split tone: highlights', [0, 0, 0], { min: -0.2, max: 0.2, step: 0.005, bind: 'look:highlightTint' }),
        sl('look.blackLevel', 'Faded blacks', 0, 0, 0.2, 0.005, { bind: 'look:blackLevel' }),
      ] },
      { title: 'Glow', items: [
        sl('look.bloom', 'Bloom', 0.04, 0, 0.5, 0.005, { bind: 'look:bloom' }),
        sl('look.bloomRadius', 'Bloom size', 0.02, 0.002, 0.2, 0.002, { bind: 'look:bloomRadius' }),
        sl('look.halation', 'Halation (red film glow)', 0, 0, 1, 0.01, { bind: 'look:halation' }),
      ] },
      { title: 'Lens', items: [
        sl('look.vignette', 'Vignette', 0.25, 0, 1, 0.01, { bind: 'look:vignette' }),
        sl('look.chromaticAberration', 'Colour fringes', 0, 0, 6, 0.1, { bind: 'look:chromaticAberration', unit: 'px' }),
        sl('look.distortion', 'Distortion', 0, -0.3, 0.3, 0.005, { bind: 'look:distortion', help: '+ barrel, − pincushion.' }),
        sl('look.sharpen', 'Sharpen', 0, 0, 1, 0.01, { bind: 'look:sharpen' }),
      ] },
      { title: 'Film stock', items: [
        sl('look.grain', 'Grain', 0, 0, 0.3, 0.005, { bind: 'look:grain' }),
        sl('look.grainSize', 'Grain size', 1, 0.5, 4, 0.05, { bind: 'look:grainSize' }),
        en('look.letterbox', 'Cinema bars', 0, opts([0, 'None'], [1.85, '1.85 : 1'], [2, '2 : 1'], [2.39, '2.39 : 1 (scope)'], [2.76, '2.76 : 1 (Ultra Panavision)']), { bind: 'look:letterbox' }),
      ] },
    ] },

    { id: 'camera', title: 'Camera & exposure', icon: '◎', groups: [
      { title: 'Exposure', items: [
        tog('cam.autoExposure', 'Automatic exposure', true, { bind: 'world:autoExposure', help: 'Meter the scene, like a camera on auto (eye-like).' }),
        sl('cam.exposureCompensation', 'Exposure compensation', 0, -5, 5, 0.1, { unit: 'stops', bind: 'world:exposureCompensation' }),
        num('cam.iso', 'ISO', 100, { min: 25, max: 102400, bind: 'world:iso' }),
        num('cam.shutter', 'Shutter', 0.0208, { min: 0.0001, max: 1, step: 0.0001, unit: 's', bind: 'world:shutter', fmt: (v) => '1/' + Math.round(1 / v) }),
        sl('cam.adaptSpeed', 'Eye adaptation speed', 1.5, 0.1, 10, 0.1, { unit: 'stops/s' }),
        sl('cam.minEv', 'Darkest exposure', -4, -10, 10, 0.5, { unit: 'EV' }),
        sl('cam.maxEv', 'Brightest exposure', 16, 0, 20, 0.5, { unit: 'EV' }),
      ] },
      { title: 'Lens', items: [
        sl('cam.focalLength', 'Focal length', 35, 8, 300, 1, { unit: 'mm', bind: 'world:focalLength' }),
        sl('cam.fStop', 'Aperture', 2.8, 0.95, 32, 0.05, { unit: 'f/', bind: 'world:fStop' }),
        sl('cam.focusDistance', 'Focus distance', 0, 0, 100, 0.1, { unit: 'm', bind: 'world:focusDistance', fmt: (v) => (v <= 0 ? 'At target' : v + ' m') }),
        sl('cam.blades', 'Aperture blades', 7, 0, 16, 1, { bind: 'world:blades', fmt: (v) => (v === 0 ? 'Round' : v) }),
        en('cam.sensor', 'Sensor', 'ff', opts(['ff', 'Full frame 36×24'], ['s35', 'Super 35'], ['apsc', 'APS-C'], ['m43', 'Micro 4/3'], ['imax', 'IMAX 65'])),
      ] },
      { title: 'Motion', items: [
        tog('cam.motionBlur', 'Live motion blur', false),
        sl('cam.shake', 'Handheld shake', 0, 0, 1, 0.01),
        sl('cam.smoothing', 'Look smoothing', 18, 1, 60, 1, { help: 'How quickly the view eases to where the mouse points.' }),
      ] },
    ] },

    { id: 'sky', title: 'Sky, sun & fog', icon: '☀', groups: [
      { title: 'Fog', items: [
        sl('fog.density', 'Density', 0, 0, 0.2, 0.001, { bind: 'world:fogDensity', help: 'Per metre. 0.01 is a light haze.' }),
        v3('fog.albedo', 'Colour', [0.9, 0.9, 0.9], { min: 0, max: 1, step: 0.01, bind: 'world:fogAlbedo' }),
        sl('fog.anisotropy', 'God rays (forward scattering)', 0.6, -0.9, 0.95, 0.01, { bind: 'world:fogAnisotropy' }),
        sl('fog.top', 'Fog top', 1000000, 0, 1000000, 1, { unit: 'm', bind: 'world:fogTop', fmt: (v) => (v >= 1e6 ? 'No limit' : v + ' m') }),
      ] },
      { title: 'Sky', items: [
        en('sky.kind', 'Sky', 'physical', opts(['physical', 'Physical (Rayleigh + Mie + ozone)'], ['color', 'Flat colour'], ['none', 'None'])),
        sl('sky.turbidity', 'Haze', 2.5, 1.7, 10, 0.1, { help: '2 clear … 10 smoggy.' }),
        sl('sky.intensity', 'Brightness', 1, 0, 4, 0.05),
        sl('sky.stars', 'Stars', 0, 0, 1, 0.01),
        col('sky.color', 'Flat sky colour', '#6699ff'),
        v3('sky.ground', 'Ground colour', [0.3, 0.3, 0.3], { min: 0, max: 1, step: 0.01 }),
      ] },
      { title: 'Sun', items: [
        sl('sun.elevation', 'Elevation', 45, -10, 90, 0.5, { unit: '°' }),
        sl('sun.azimuth', 'Toward', 180, 0, 360, 1, { unit: '°' }),
        num('sun.lux', 'Brightness', 100000, { min: 0, max: 150000, unit: 'lux' }),
        sl('sun.size', 'Angular size', 0.27, 0.05, 5, 0.01, { unit: '°' }),
        tog('sun.dayCycle', 'Day cycle', false),
        sl('sun.daySpeed', 'Day length', 24, 1, 1440, 1, { unit: 'min' }),
      ] },
    ] },

    { id: 'world', title: 'World', icon: '⬢', groups: [
      { title: 'Size and streaming', items: [
        sl('world.drawDistance', 'Draw distance', 2000, 50, 20000, 50, { unit: 'm' }),
        tog('world.streaming', 'Stream big worlds', false),
        sl('world.cellSize', 'Streaming cell', 256, 32, 2048, 32, { unit: 'm' }),
        tog('world.gpuCulling', 'GPU culling', false),
        tog('world.instancing', 'Instancing', true, { bind: 'editor', always: true, help: 'Same shapes are drawn at once. Always on.' }),
      ] },
      { title: 'Detail', items: [
        sl('world.lodBias', 'Level-of-detail bias', 0, -2, 2, 0.1),
        sl('world.foliageDensity', 'Foliage density', 1, 0, 2, 0.05),
        tog('world.impostors', 'Flat cut-outs far away', false),
        sl('world.patternFade', 'Pattern detail fade', 1, 0.25, 4, 0.25, { unit: 'px' }),
        sl('world.terrainRes', 'Terrain resolution', 1, 0.25, 4, 0.25),
      ] },
    ] },

    { id: 'physics', title: 'Physics', icon: '⚛', groups: [
      { title: 'Simulation', items: [
        v3('physics.gravity', 'Gravity', [0, -9.81, 0], { min: -30, max: 30, step: 0.01, unit: 'm/s²' }),
        en('physics.rate', 'Steps per second', 120, opts(60, 120, 240)),
        sl('physics.iterations', 'Solver iterations', 10, 1, 50, 1),
        sl('physics.sleep', 'Sleep below', 0.05, 0, 1, 0.01, { unit: 'm/s' }),
        tog('physics.ccd', 'Continuous collisions (fast things)', false),
      ] },
      { title: 'Defaults', items: [
        sl('physics.friction', 'Friction', 0.6, 0, 2, 0.01),
        sl('physics.bounce', 'Bounce', 0.2, 0, 1, 0.01),
        sl('physics.density', 'Density', 1000, 10, 20000, 10, { unit: 'kg/m³' }),
      ] },
      { title: 'Walking body', items: [
        sl('physics.charRadius', 'Radius', 0.25, 0.1, 1, 0.01, { unit: 'm' }),
        sl('physics.charStep', 'Step height', 0.3, 0, 1, 0.01, { unit: 'm' }),
        sl('physics.charSlope', 'Steepest walkable', 45, 10, 80, 1, { unit: '°' }),
        sl('physics.charSkin', 'Skin width', 0.55, 0.1, 1, 0.01),
      ] },
      { title: 'Cloth and ropes', items: [
        sl('physics.clothIterations', 'Cloth iterations', 8, 1, 40, 1),
        sl('physics.ropeIterations', 'Rope iterations', 12, 1, 40, 1),
        sl('physics.wind', 'Wind', 0, 0, 20, 0.1, { unit: 'm/s' }),
      ] },
    ] },

    { id: 'player', title: 'Player & controls', icon: '⌖', groups: [
      { title: 'Movement', items: [
        sl('player.walk', 'Walk speed', 1.6, 0.2, 10, 0.1, { unit: 'm/s', bind: 'opt:walk' }),
        sl('player.run', 'Run speed', 4.5, 0.5, 20, 0.1, { unit: 'm/s', bind: 'opt:run' }),
        sl('player.eye', 'Eye height', 1.6, 0.5, 3, 0.01, { unit: 'm' }),
        sl('player.bob', 'Head bob', 0.025, 0, 0.1, 0.001, { unit: 'm' }),
        sl('player.accel', 'Start and stop', 7, 1, 30, 0.5),
      ] },
      { title: 'Looking', items: [
        sl('player.look', 'Mouse sensitivity', 1, 0.1, 5, 0.05, { bind: 'opt:look', unit: 'x' }),
        tog('player.invertY', 'Invert vertical look', false),
        sl('player.touchLook', 'Touch look speed', 1.6, 0.2, 5, 0.1, { unit: 'x' }),
        sl('player.stickSize', 'Thumb stick size', 110, 60, 200, 5, { unit: 'px' }),
        sl('player.deadzone', 'Gamepad dead zone', 0.15, 0, 0.5, 0.01),
      ] },
      { title: 'Key bindings', items: [
        key('keys.forward', 'Forward', 'KeyW'),
        key('keys.back', 'Back', 'KeyS'),
        key('keys.left', 'Left', 'KeyA'),
        key('keys.right', 'Right', 'KeyD'),
        key('keys.run', 'Run', 'ShiftLeft'),
        key('keys.use', 'Use / talk', 'KeyE'),
        key('keys.jump', 'Jump', 'Space'),
        key('keys.crouch', 'Crouch', 'KeyC'),
        key('keys.pause', 'Pause menu', 'Escape'),
      ] },
    ] },

    { id: 'animation', title: 'Animation & people', icon: '☺', groups: [
      { title: 'Bodies', items: [
        tog('anim.ik', 'Feet planted with IK', true),
        tog('anim.breathing', 'Breathing', true),
        tog('anim.armSwing', 'Arm swing', true),
        sl('anim.blend', 'Blend time', 0.25, 0, 1, 0.01, { unit: 's' }),
        en('anim.bodyStyle', 'Body', 'capsules', opts(['capsules', 'Capsules'], ['skinned', 'Skinned mesh'])),
        tog('anim.faces', 'Faces', false),
        tog('anim.hair', 'Hair', false),
      ] },
      { title: 'Crowds', items: [
        tog('anim.steering', 'Walk around obstacles', false, { help: 'People now walk straight to where they\'re sent.' }),
        sl('anim.crowdLod', 'Simple bodies beyond', 30, 5, 200, 1, { unit: 'm' }),
        num('anim.maxPeople', 'Most people', 200, { min: 1, max: 5000 }),
      ] },
    ] },

    { id: 'audio', title: 'Audio', icon: '♪', groups: [
      { title: 'Volume', items: [
        sl('audio.master', 'Master', 1, 0, 1, 0.01),
        sl('audio.ambience', 'Ambience', 1, 0, 1, 0.01),
        sl('audio.effects', 'Effects', 1, 0, 1, 0.01),
        sl('audio.footsteps', 'Footsteps', 1, 0, 1, 0.01),
        sl('audio.dialogue', 'Dialogue', 1, 0, 1, 0.01),
        sl('audio.music', 'Music', 1, 0, 1, 0.01),
      ] },
      { title: 'Space', items: [
        sl('audio.reverb', 'Room echo', 1, 0, 2, 0.01),
        sl('audio.distance', 'Distance fall-off', 1, 0.1, 4, 0.05),
        tog('audio.earDelay', 'Ear delay (3D)', true),
        tog('audio.airAbsorb', 'Air absorption', true),
        tog('audio.doppler', 'Doppler', false),
        tog('audio.limiter', 'Limiter', true),
      ] },
      { title: 'Output', items: [
        en('audio.rate', 'Sample rate', 48000, opts(44100, 48000)),
        en('audio.buffer', 'Buffer', 2048, opts(256, 512, 1024, 2048, 4096), { help: 'Smaller is less delay but more risk of crackles.' }),
        tog('audio.worklet', 'Mix on its own thread (AudioWorklet)', false),
      ] },
    ] },

    { id: 'scripting', title: 'Scripting', icon: '{}', groups: [
      { title: 'Clock', items: [
        en('script.tick', 'Script steps per second', 120, opts(60, 120, 240)),
        sl('script.catchUp', 'Most catch-up per frame', 8, 1, 50, 1, { unit: 'ms' }),
        sl('script.maxSteps', 'Most steps per frame', 12, 1, 60, 1),
        sl('script.timeScale', 'World speed', 1, 0, 4, 0.05, { bind: 'opt:timescale', unit: 'x', help: 'Slow motion and fast forward (walking keeps real time).' }),
      ] },
      { title: 'Errors', items: [
        en('script.onError', 'On an error', 'continue', opts(['continue', 'Keep running'], ['stop', 'Stop the scene'], ['pause', 'Pause'])),
        tog('script.didYouMean', '"Did you mean…?"', true, { bind: 'editor', always: true }),
        tog('script.strict', 'Warn about unused names', false),
      ] },
      { title: 'Testing', items: [
        tog('script.autoplay', 'Autoplay (the player counts as near everything)', false, { bind: 'autoplay' }),
        txt('script.choices', 'Story choices for tests', '0', { help: 'Comma list, as for loop play --choices.' }),
      ] },
    ] },

    { id: 'story', title: 'Story & UI', icon: '❝', groups: [
      { title: 'Dialogue', items: [
        sl('story.textSpeed', 'Reading speed', 15, 5, 40, 1, { unit: 'chars/s' }),
        sl('story.lineMin', 'Shortest line', 2, 0.5, 10, 0.1, { unit: 's' }),
        sl('story.subtitleSize', 'Subtitle size', 1, 0.6, 2, 0.05, { unit: 'x' }),
        tog('story.speakerNames', 'Show speaker names', true),
        col('story.speakerColor', 'Speaker colour', '#ffd27a'),
      ] },
      { title: 'Choices', items: [
        sl('story.choiceTimeout', 'Choice timeout', 0, 0, 60, 1, { unit: 's', fmt: (v) => (v ? v + ' s' : 'Wait') }),
        en('story.timeoutPick', 'When time runs out', 0, opts([0, 'First option'], [-1, 'Random'])),
      ] },
      { title: 'Between scenes', items: [
        sl('story.fade', 'Fade', 0.6, 0, 3, 0.1, { unit: 's' }),
        txt('story.loadingText', 'Loading text', 'Loading {scene}…'),
        tog('story.hud', 'Show the HUD', true),
      ] },
    ] },

    { id: 'performance', title: 'Performance & benchmark', icon: '⏱', groups: [
      { title: 'Benchmark', items: [
        sl('perf.benchSeconds', 'Seconds per scene', 20, 5, 120, 1, { unit: 's', bind: 'editor' }),
        sl('perf.hitch', 'A hitch is a frame over', 25, 10, 100, 1, { unit: 'ms', help: 'Or twice the median frame, whichever is longer.' }),
        txt('perf.tracker', 'Smoothness Tracker link', ''),
      ] },
      { title: 'Threads', items: [
        tog('perf.loadWorker', 'Build scenes off the main thread', false),
        tog('perf.asyncPipelines', 'Compile GPU pipelines in the background', false),
        tog('perf.wasmThreads', 'WASM threads', false),
        tog('perf.simd', 'WASM SIMD', false),
        tog('perf.gpuTiming', 'GPU timestamp queries', false),
      ] },
    ] },

    { id: 'build', title: 'Build & publish', icon: '⇪', groups: [
      { title: 'Web build', items: [
        txt('build.out', 'Output folder', 'site'),
        tog('build.includeAll', 'Include every project', true),
        tog('build.stills', 'Include trailer stills', true),
        en('build.opt', 'Optimisation', 'O3', opts('O0', 'O1', 'O2', 'O3', 'Os', 'Oz')),
        tog('build.compress', 'Pre-compress (brotli)', false),
        txt('build.emsdk', 'Emscripten version', '6.0.13'),
      ] },
      { title: 'Publishing', items: [
        en('build.target', 'Publish to', 'pages', opts(['pages', 'GitHub Pages (manual workflow)'], ['zip', 'A zip file'], ['folder', 'A folder'])),
        tog('build.stamp', 'Stamp the build on the page', true),
      ] },
      { title: 'Platforms', items: [
        tog('build.iphone', 'iPhone and iPad (Safari 26+)', true),
        tog('build.desktop', 'Desktop browsers (Chrome, Edge, Safari)', true),
        tog('build.android', 'Android (Chrome)', true),
        tog('build.native', 'Native app (Windows, macOS)', false),
      ] },
    ] },
  ];

  // ---------------------------------------------------------------- editor
  const editor = [
    { id: 'appearance', title: 'Appearance', icon: '◑', groups: [
      { title: 'Theme', items: [
        en('ed.theme', 'Theme', 'dark', opts(['dark', 'Dark'], ['midnight', 'Midnight'], ['light', 'Light']), { bind: 'editor' }),
        col('ed.accent', 'Accent', '#4c9dff', { bind: 'editor' }),
        sl('ed.fontSize', 'Interface text size', 12, 10, 18, 1, { unit: 'px', bind: 'editor' }),
        en('ed.density', 'Density', 'comfortable', opts('compact', 'comfortable', 'spacious'), { bind: 'editor' }),
        tog('ed.tooltips', 'Tooltips', true, { bind: 'editor' }),
        tog('ed.badges', 'Show "Not wired" badges', true, { bind: 'editor' }),
      ] },
    ] },
    { id: 'layout', title: 'Layout', icon: '▤', groups: [
      { title: 'Window', items: [
        en('ed.layout', 'Layout', 'default', opts(['default', 'Default'], ['scripting', 'Scripting'], ['lighting', 'Look & lighting'], ['performance', 'Performance'], ['viewport', 'Viewport only']), { bind: 'editor' }),
        tog('ed.toolbar', 'Toolbar', true, { bind: 'editor' }),
        tog('ed.statusbar', 'Status bar', true, { bind: 'editor' }),
        tog('ed.rememberLayout', 'Remember panel layout', true, { bind: 'editor' }),
      ] },
    ] },
    { id: 'viewport', title: 'Viewport', icon: '▢', groups: [
      { title: 'Overlays', items: [
        tog('vp.stats', 'Stats overlay', true, { bind: 'editor' }),
        tog('vp.grid', 'Grid', false, { bind: 'editor' }),
        tog('vp.thirds', 'Rule of thirds', false, { bind: 'editor' }),
        tog('vp.safe', 'Safe frames', false, { bind: 'editor' }),
        tog('vp.crosshair', 'Centre mark', false, { bind: 'editor' }),
        sl('vp.statsEvery', 'Refresh stats every', 500, 100, 2000, 50, { unit: 'ms', bind: 'editor' }),
      ] },
      { title: 'Navigation', items: [
        en('vp.drag', 'Look with', 'any', opts(['any', 'Any mouse button'], ['right', 'Right button (Unreal style)']), { bind: 'editor' }),
        tog('vp.keysNeedFocus', 'Keys move only when the viewport has focus', true, { bind: 'editor' }),
        sl('vp.flySpeed', 'Editor camera speed', 5, 0.5, 50, 0.5, { unit: 'm/s' }),
      ] },
    ] },
    { id: 'gizmos', title: 'Gizmos & snapping', icon: '✣', groups: [
      { title: 'Snapping', items: [
        tog('snap.move', 'Snap moves', true, {}),
        en('snap.moveStep', 'Move step', 0.25, opts(0.01, 0.05, 0.1, 0.25, 0.5, 1, 5, 10), { unit: 'm' }),
        tog('snap.rotate', 'Snap turns', true, {}),
        en('snap.rotateStep', 'Turn step', 15, opts(1, 5, 10, 15, 30, 45, 90), { unit: '°' }),
        tog('snap.scale', 'Snap scale', false, {}),
        en('snap.scaleStep', 'Scale step', 0.1, opts(0.01, 0.05, 0.1, 0.25, 0.5, 1), {}),
      ] },
      { title: 'Gizmo', items: [
        sl('gizmo.size', 'Size', 1, 0.5, 3, 0.1, { unit: 'x' }),
        en('gizmo.space', 'Space', 'world', opts('world', 'local'), {}),
      ] },
    ] },
    { id: 'code', title: 'Script editor', icon: '✎', groups: [
      { title: 'Editing', items: [
        sl('code.fontSize', 'Text size', 13, 10, 22, 1, { unit: 'px', bind: 'editor' }),
        en('code.tab', 'Tab width', 2, opts(2, 4), { bind: 'editor' }),
        tog('code.wrap', 'Wrap long lines', false, { bind: 'editor' }),
        tog('code.lineNumbers', 'Line numbers', true, { bind: 'editor' }),
        tog('code.runOnSave', 'Run on Ctrl+S', true, { bind: 'editor' }),
        tog('code.autoIndent', 'Keep indentation on Enter', true, { bind: 'editor' }),
        tog('code.autocomplete', 'Word suggestions', false),
        tog('code.minimap', 'Minimap', false),
      ] },
    ] },
    { id: 'console', title: 'Console', icon: '›_', groups: [
      { title: 'Messages', items: [
        sl('con.max', 'Keep at most', 1000, 100, 10000, 100, { unit: 'lines', bind: 'editor' }),
        tog('con.time', 'Timestamps', true, { bind: 'editor' }),
        tog('con.clearOnLoad', 'Clear when a scene loads', false, { bind: 'editor' }),
        tog('con.engine', 'Engine messages', true, { bind: 'editor' }),
        tog('con.lines', 'Dialogue lines', true, { bind: 'editor' }),
      ] },
    ] },
    { id: 'autosave', title: 'Autosave', icon: '⟳', groups: [
      { title: 'Autosave', items: [
        tog('save.auto', 'Keep script edits in this browser', true, { bind: 'editor' }),
        sl('save.every', 'Every', 5, 1, 60, 1, { unit: 's', bind: 'editor' }),
      ] },
    ] },
  ];

  LE.SCHEMA = { project, editor };
})();
