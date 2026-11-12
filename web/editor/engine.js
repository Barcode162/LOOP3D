// The editor's link to the engine: loads player.wasm, answers its calls
// (window.loopReady, loopUi, …) and gives the editor plain functions.
window.LE = window.LE || {};
(() => {
  const listeners = {};
  const E = {
    M: null, ready: false, index: { projects: [] }, scene: null, sceneText: '', quality: 1, movie: false,
    on(ev, fn) { (listeners[ev] = listeners[ev] || []).push(fn); },
    emit(ev, ...a) { (listeners[ev] || []).forEach((fn) => { try { fn(...a); } catch (e) { console.error(e); } }); },
  };
  LE.engine = E;

  // ---- strings in and out of the engine
  function withStr(text, fn) {
    const M = E.M, n = M.lengthBytesUTF8(text) + 1, p = M._malloc(n);
    M.stringToUTF8(text, p, n);
    try { return fn(p); } finally { M._free(p); }
  }
  const read = (p) => E.M.UTF8ToString(p);
  const has = (name) => E.M && typeof E.M['_' + name] === 'function';

  E.has = has;
  E.setQuality = (q) => { E.quality = q; if (E.M) E.M._loop_set_quality(q); E.emit('quality', q); };
  E.setOption = (name, value) => (has('loop_set_option') ? withStr(name, (p) => E.M._loop_set_option(p, value)) : 0);
  E.setLook = (obj) => (has('loop_set_look') ? withStr(JSON.stringify(obj), (p) => E.M._loop_set_look(p)) : 0);
  E.setWorld = (obj) => (has('loop_set_world') ? withStr(JSON.stringify(obj), (p) => E.M._loop_set_world(p)) : 0);
  E.setAutoplay = (on) => E.M && E.M._loop_set_autoplay(on ? 1 : 0);
  // engine settings by name (engine/core/settings.h); null clears the editor's value
  E.setSetting = (id, value) => (has('loop_set_setting') ? withStr(id, (p) => withStr(JSON.stringify(value === undefined ? null : value), (q) => E.M._loop_set_setting(p, q))) : 0);
  E.getSetting = (id) => { if (!has('loop_get_setting')) return undefined; try { return JSON.parse(withStr(id, (p) => read(E.M._loop_get_setting(p)))); } catch (e) { return undefined; } };
  E.settingsList = () => { try { return JSON.parse(read(E.M._loop_settings_json())); } catch (e) { return []; } };
  E.setProjectSettings = (obj) => withStr(JSON.stringify(obj || {}), (p) => E.M._loop_set_project_settings(p));
  E.setMode = (movie) => { E.movie = !!movie; if (E.M) E.M._loop_set_mode(movie ? 1 : 0); E.emit('mode', E.movie); };
  E.useKey = () => E.M && withStr('use', (p) => E.M._loop_emit(p));
  E.choose = (i) => E.M && E.M._loop_choose(i);
  E.key = (code, down) => E.M && E.M._loop_key(code, down ? 1 : 0);
  E.move = (x, y) => E.M && E.M._loop_move(x, y);
  E.look = (dx, dy) => E.M && E.M._loop_look(dx, dy);
  E.stats = () => { try { return JSON.parse(read(E.M._loop_stats())); } catch (e) { return {}; } };
  E.sceneInfo = () => { if (!has('loop_scene_json')) return null; try { return JSON.parse(read(E.M._loop_scene_json())); } catch (e) { return null; } };
  E.log = () => (has('loop_log') ? read(E.M._loop_log()) : '');
  E.perfGraph = () => { const p = E.M._loop_perf_graph() >> 2; return Array.from(E.M.HEAPF32.subarray(p, p + 240)); };
  E.perfBegin = () => E.M._loop_perf_begin();
  E.perfEnd = () => JSON.parse(read(E.M._loop_perf_end()));
  // the editor's hands on the world (web/player.cpp: loop_edit_camera, loop_pick, loop_view, loop_thing, loop_thing_set)
  E.editCamera = (o) => (has('loop_edit_camera') ? withStr(JSON.stringify(o), (p) => E.M._loop_edit_camera(p)) : 0);
  E.pick = (x, y) => (has('loop_pick') ? E.M._loop_pick(x, y) : -1);
  E.view = () => { if (!has('loop_view')) return null; try { return JSON.parse(read(E.M._loop_view())); } catch (e) { return null; } };
  E.thing = (i) => { if (!has('loop_thing') || i < 0) return null; try { return JSON.parse(read(E.M._loop_thing(i))); } catch (e) { return null; } };
  E.thingSet = (i, o) => (has('loop_thing_set') ? withStr(JSON.stringify(o), (p) => E.M._loop_thing_set(i, p)) : 0);
  // keys (web/player.cpp: loop_clips, loop_clip_set, loop_clip_moment, loop_clip_curve)
  E.clips = () => { if (!has('loop_clips')) return []; try { return JSON.parse(read(E.M._loop_clips())); } catch (e) { return []; } };
  E.clipSet = (i, o) => (has('loop_clip_set') ? withStr(JSON.stringify(o), (p) => E.M._loop_clip_set(i, p)) : -1);
  E.clipMoment = (i) => (has('loop_clip_moment') ? E.M._loop_clip_moment(i) : -1);
  E.clipPlaying = (i) => (has('loop_clip_playing') ? !!E.M._loop_clip_playing(i) : false);
  E.clipScript = (i, target, indent) => (has('loop_clip_script') ? withStr(target, (p) => withStr(indent || '', (q) => read(E.M._loop_clip_script(i, p, q)))) : '');
  E.clipCurve = (i, channel, from, to, count) => {
    if (!has('loop_clip_curve')) return [];
    try { return JSON.parse(withStr(channel, (p) => read(E.M._loop_clip_curve(i, p, from, to, count)))); } catch (e) { return []; }
  };
  E.musicSet = (o) => (has('loop_music_set') ? withStr(JSON.stringify(o), (p) => E.M._loop_music_set(p)) : 0);
  E.musicStep = () => (has('loop_music_step') ? E.M._loop_music_step() : -1);
  E.capture = () => new Promise((resolve) => {
    window.loopCaptured = (w, h, ptr) => {
      const px = new Uint8ClampedArray(E.M.HEAPU8.buffer, ptr, w * h * 4).slice();
      resolve({ w, h, px });
    };
    E.M._loop_capture();
  });

  // ---- scenes and projects
  E.loadIndex = async () => {
    try { E.index = await (await fetch('scenes/index.json')).json(); } catch (e) { E.index = { projects: [] }; }
    E.emit('index', E.index);
    return E.index;
  };
  E.projectOf = (scene) => E.index.projects.find((p) => p.scenes.some((s) => s.name === scene) || (p.library || []).includes(scene));
  E.fetchScene = async (name) => {
    const r = await fetch('scenes/' + encodeURIComponent(name) + '.loop');
    if (!r.ok) throw new Error('no scene "' + name + '"');
    return r.text();
  };
  // `use "name"` brings in a script from the same folder (once)
  E.expandUses = async function expandUses(text, seen = new Set()) {
    const out = [];
    for (const line of text.split('\n')) {
      const m = line.match(/^\s*use\s+"([^"]+)"/);
      if (!m) { out.push(line); continue; }
      if (!seen.has(m[1])) {
        seen.add(m[1]);
        try { out.push(await expandUses(await E.fetchScene(m[1]), seen)); } catch (e) { out.push('say "can\'t find ' + m[1] + '.loop"'); }
      }
      out.push('');
    }
    return out.join('\n');
  };
  // Runs script text as the scene (what the Script panel's Run does).
  // Shared scripts (`use "x"`) go first and the scene's own lines follow
  // unchanged, so a problem's line number maps back by E.lineOffset (web/loop_ui.js).
  E.lineOffset = 0;
  E.runText = async (text, name) => {
    const a = await LoopUI.assemble(text, E.fetchScene);
    E.lineOffset = a.offset;
    E.emit('loading', name || E.scene);
    await LoopUI.loadModels(E.M, a.text);
    await new Promise((r) => requestAnimationFrame(() => setTimeout(r, 0)));
    withStr(a.text, (p) => E.M._loop_run_script(p));
  };
  E.loadScene = async (name) => {
    const text = await E.fetchScene(name);
    E.scene = name;
    E.sceneText = text;
    E.emit('sceneText', name, text);
    // the editor may run its own copy (edits kept in this browser)
    await E.runText(E.textFor ? E.textFor(name, text) : text, name);
  };

  // ---- the engine's calls to the page
  addEventListener('unhandledrejection', (e) => { if (String(e.reason).includes('external Instance reference')) e.preventDefault(); });
  window.loopNoGpu = () => E.emit('nogpu');
  window.loopProblems = (t) => E.emit('problems', t);
  window.loopGpuProblem = (t) => E.emit('log', 'error', 'Graphics: ' + t);
  window.loopShown = () => E.emit('shown');
  window.loopGpuLost = () => E.emit('gpulost');
  window.loopGpuBack = (q) => { E.quality = q; E.emit('gpuback', q); E.emit('quality', q); };
  window.loopSay = (t) => E.emit('log', 'say', t);
  window.loopUi = (kind, speaker, text, options, seconds) => E.emit('ui', { kind, speaker, text, options: options ? options.split('\n') : [], seconds });
  window.loopStarted = (title, mode, shots) => { E.movie = mode === 'movie' && shots > 0; E.emit('started', { title, mode, shots }); E.emit('mode', E.movie); };
  window.loopGoTo = (name) => { E.emit('goto', name); E.loadScene(name).catch((e) => E.emit('log', 'error', String(e.message || e))); };
  // the player starts on Phone on phones and High elsewhere (web/player.cpp main)
  window.loopReady = () => { E.ready = true; E.quality = /iPhone|iPad|Android/i.test(navigator.userAgent) ? 0 : 1; E.emit('ready'); };

  E.boot = async () => {
    window.Module = { onRuntimeInitialized() { E.M = window.Module; }, print: (t) => E.emit('log', 'info', t), printErr: (t) => E.emit('log', 'warn', t) };
    // every core when the page may share memory between them (isolate.js), else one
    const many = window.LoopIsolate && await LoopIsolate.ready() && typeof SharedArrayBuffer !== 'undefined';
    const s = document.createElement('script');
    s.src = many ? 'player_mt.js' : 'player.js';
    s.onerror = () => E.emit('log', 'error', 'Couldn\'t load ' + s.src.split('/').pop() + '. Build the site with tools/build_site.sh.');
    document.body.appendChild(s);
  };
})();
