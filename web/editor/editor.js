// The Loop3D editor: boots the engine, builds the menus and commands, joins
// settings to the engine, and drives the viewport, toolbar and status bar.
(() => {
  const { h, $, store, dock } = LE;
  const E = LE.engine;
  const P = LE.panels;
  const params = new URLSearchParams(location.search);
  const touch = matchMedia('(pointer: coarse)').matches;
  if (touch) document.body.classList.add('touch');
  const QN = ['Phone', 'High', 'Ultra'];
  let paused = false, booted = false, audioCtx = null, trailerOn = false, split = 0.5, tool = 'select', autoAsked = 0;

  // ------------------------------------------------------------ settings → engine
  let lookT = 0, worldT = 0;
  const pushLook = () => {
    clearTimeout(lookT);
    lookT = setTimeout(() => {
      const o = {};
      for (const it of store.items.values()) if (it.bind && it.bind.startsWith('look:') && store.isModified(it.id)) o[it.bind.slice(5)] = store.get(it.id);
      if (E.M) E.setLook(o);
    }, 16);
  };
  const pushWorld = () => {
    clearTimeout(worldT);
    worldT = setTimeout(() => {
      const o = {};
      for (const it of store.items.values()) if (it.bind && it.bind.startsWith('world:') && store.isModified(it.id)) o[it.bind.slice(6)] = store.get(it.id);
      if (E.M) E.setWorld(o);
    }, 16);
  };
  // engine settings (bind 'set'): the editor's layer holds only what differs
  // from the project's value; null gives the project's or scene's back
  function sendSetting(it) {
    if (!E.M) return;
    const v = store.isModified(it.id) ? store.get(it.id) : null;
    if (!E.setSetting(it.id, v) && v !== null) LE.log('warn', 'The engine has no setting ' + it.id);
  }
  function applyEngine(it) {
    if (!E.M || !it.bind) return;
    const v = store.get(it.id), b = it.bind;
    if (b === 'quality') { E.setQuality(+v); syncToolbar(); }
    else if (b === 'set') { sendSetting(it); if (it.id.startsWith('render.') || it.id.startsWith('script.')) syncToolbar(); }
    else if (b.startsWith('look:')) pushLook();
    else if (b.startsWith('world:')) pushWorld();
    else if (b === 'project' || b === 'build') LE.toast(it.label + ': kept for this project. File → Save to project writes project.json.');
    if (it.id === 'render.showLights' || it.id === 'render.showBounds') drawOverlay();
  }
  function applyAllEngine() {
    // the project's own settings (project.json) first, then the editor's changes over them
    const proj = E.projectOf(E.scene) || E.index.projects.find((p) => p.name === store.project);
    if (proj && E.has('loop_set_project_settings')) E.setProjectSettings(proj.settings || {});
    for (const it of store.items.values()) {
      if (!it.bind || it.scope !== 'project') continue;
      if (it.bind === 'quality') { if (store.isModified(it.id) || params.get('quality')) E.setQuality(+(params.get('quality') ?? store.get(it.id))); continue; }
      if (it.bind === 'set') sendSetting(it);
    }
    pushLook(); pushWorld();
  }
  function applyEditor(id) {
    const g = (k) => store.get(k);
    const B = document.body;
    if (!id || id === 'ed.theme') B.dataset.theme = g('ed.theme');
    if (!id || id === 'ed.accent') document.documentElement.style.setProperty('--accent', g('ed.accent'));
    if (!id || id === 'ed.fontSize') document.documentElement.style.setProperty('--fs', g('ed.fontSize') + 'px');
    if (!id || id === 'code.fontSize') document.documentElement.style.setProperty('--code-fs', g('code.fontSize') + 'px');
    if (!id || id === 'ed.density') B.dataset.density = g('ed.density');
    if (!id || id === 'ed.toolbar') B.classList.toggle('no-toolbar', !g('ed.toolbar'));
    if (!id || id === 'ed.statusbar') B.classList.toggle('no-statusbar', !g('ed.statusbar'));
    if (!id || id === 'ed.badges') B.classList.toggle('no-badges', !g('ed.badges'));
    if (!id || id === 'vp.stats') B.classList.toggle('no-stats', !g('vp.stats'));
    if (!id || id === 'code.lineNumbers') B.classList.toggle('no-lines', !g('code.lineNumbers'));
    if (!id || id === 'code.wrap') B.classList.toggle('wrap', g('code.wrap'));
    if (!id || id === 'con.time') B.classList.toggle('no-time', !g('con.time'));
    if (!id || id === 'code.tab') document.querySelectorAll('.code pre, .code textarea').forEach((e) => { e.style.tabSize = g('code.tab'); });
    if (!id || id === 'ed.tooltips') { if (!g('ed.tooltips')) document.querySelectorAll('[title]').forEach((e) => { e.dataset.t = e.title; e.removeAttribute('title'); }); else document.querySelectorAll('[data-t]').forEach((e) => { e.title = e.dataset.t; }); }
    if (id === 'ed.layout') dock.preset(g('ed.layout'));
    if (!id || id.startsWith('vp.') || id.startsWith('snap.') || id === 'gizmo.space') { drawOverlay(); syncToolbar(); }
    if (id === 'vp.statsEvery') startStats();
    if (P.script && (id === 'code.wrap' || id === 'code.fontSize' || id === 'code.lineNumbers' || id === 'code.minimap')) P.script.paint();
  }
  store.on((id, v, it) => {
    if (it.scope === 'editor') applyEditor(id);
    else applyEngine(it);
    if (it.id.startsWith('look.') || it.id.startsWith('cam.') || it.id.startsWith('fog.')) {
      // keep other open views of the same setting in step
      document.querySelectorAll('.prop[data-id="' + CSS.escape(id) + '"]').forEach((r) => r.refresh && document.activeElement?.closest('.prop') !== r && r.refresh());
    }
    LE.status();
  });

  // ------------------------------------------------------------ commands
  const C = LE.cmd;
  C('play', 'Play (walk around)', () => { if (paused) togglePause(); E.setMode(false); $('#view').focus(); vpFocus(true); }, { key: 'F5' });
  C('movie', 'Play the film (Movie mode)', () => { if (paused) togglePause(); E.setMode(true); }, { key: 'F6' });
  C('pause', 'Pause / resume the world', () => togglePause(), { key: 'F7' });
  C('step', 'Step one frame', () => { if (!paused) togglePause(); E.setOption('step', 1); }, { key: 'F8' });
  C('stop', 'Restart the scene', () => { if (paused) togglePause(); P.script.run(); }, { key: 'Shift+F5' });
  C('projectSettings', 'Project Settings…', (id) => LE.settingsWindow('project', id), { key: 'Ctrl+,' });
  C('editorPrefs', 'Editor Preferences…', (id) => LE.settingsWindow('editor', id));
  C('palette', 'Command palette…', () => LE.palette(), { key: 'Ctrl+P', hidden: true });
  C('openScene', 'Open scene…', (name) => (name ? openScene(name) : LE.palette('')), { key: 'Ctrl+O' });
  C('openLibrary', 'Open shared script', async (name) => { const t = await E.fetchScene(name); P.script.load(name, t, false); dock.show('script'); }, { hidden: true });
  C('newScene', 'New scene', () => newScene(), { key: 'Ctrl+N' });
  C('runScript', 'Run the script', () => P.script.run(), { key: 'Ctrl+Enter' });
  C('saveScript', 'Download the script (.loop)', () => P.script.download(), { key: 'Ctrl+Shift+S' });
  C('revertScript', 'Revert the script', () => P.script.revert());
  C('find', 'Find in script', () => P.script.find(), { key: 'Ctrl+F' });
  C('capture', 'Save a screenshot', () => capture());
  C('benchmark', 'Benchmark this scene', () => benchmark());
  C('trailer', 'Compare with the cinema still', () => setTrailer(!trailerOn), { key: 'T' });
  C('sound', 'Sound on / off', () => toggleSound());
  C('fullscreen', 'Full screen', () => (document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen?.()), { key: 'F11' });
  C('maximize', 'Viewport only / all panels', () => { const all = ['left', 'right', 'bottom']; const s = dock.state; s.hidden = s.hidden && s.hidden.length === 3 ? [] : all; dock.render(); dock.save(); }, { key: 'Shift+Space' });
  C('restartViewport', 'Restart viewport (apply MSAA and other restart settings)', () => restartViewport());
  C('resetLayout', 'Reset layout', () => { store.set('ed.layout', 'default'); dock.preset('default'); });
  C('help', 'Keyboard shortcuts', () => shortcuts(), { key: 'F1' });
  C('reference', 'Looped Script quick reference', () => reference());
  C('about', 'About Loop3D', () => about());
  C('buildSite', 'Build the web site…', () => LE.info('Build the site', '<p>The site is built on a computer from the repository (a browser can\'t run the compiler):</p><pre class="mono">./tools/build_site.sh\ncd site && python3 -m http.server 8000</pre><p>It reads each project\'s <code>project.json</code>, including the Build settings saved there with <b>File → Save to project</b>.</p>'));
  C('renderStill', 'Render a cinema still…', () => LE.info('Render a cinema still', '<p>Cinema renders run on a computer with the <code>loop</code> tool, which reads the project\'s settings (Project Settings → Cinema renderer, saved with File → Save to project):</p><pre class="mono">./build/loop render projects/' + (E.projectOf(E.scene)?.name || '&lt;project&gt;') + '/' + (E.scene || 'scene') + '.loop</pre>'));
  C('clearConsole', 'Clear the console', () => { P.console.entries = []; P.console.draw(); });
  C('playInPlayer', 'Play in the player (what\'s loaded, edits and all)', () => playInPlayer(), { key: 'Ctrl+Shift+P' });
  C('saveProject', 'Save to project (project.json)…', () => saveProject());
  for (const p of document.querySelectorAll('#panels .panel')) C('panel:' + p.dataset.panel, 'Show ' + p.dataset.title, () => dock.show(p.dataset.panel), { cat: 'Panel' });
  for (const name of Object.keys(LE.LAYOUTS)) C('layout:' + name, 'Layout: ' + name, () => store.set('ed.layout', name), { cat: 'Layout' });
  for (const [i, n] of QN.entries()) C('quality:' + i, 'Quality: ' + n, () => store.set('render.quality', i), { cat: 'Quality' });
  for (const l of LE.LOOKS) C('look:' + l, 'Look: ' + l, () => store.set('look.preset', l), { cat: 'Look' });

  // ------------------------------------------------------------ menus
  const panelsMenu = () => [...document.querySelectorAll('.panel')].map((p) => ({ label: p.dataset.title, check: () => dock.isOpen(p.dataset.panel), run: () => dock.toggle(p.dataset.panel), disabled: () => p.dataset.panel === 'viewport' }));
  const MENUS = {
    File: () => [{ label: 'New scene', cmd: 'newScene', key: 'Ctrl+N' }, { label: 'Open scene…', cmd: 'openScene', key: 'Ctrl+O' }, '-', { label: 'Run script', cmd: 'runScript', key: 'Ctrl+Enter' }, { label: 'Play in the player', cmd: 'playInPlayer', key: 'Ctrl+Shift+P' }, { label: 'Download script (.loop)', cmd: 'saveScript', key: 'Ctrl+Shift+S' }, { label: 'Revert script', cmd: 'revertScript' }, '-', { label: 'Save to project (project.json)…', cmd: 'saveProject' }, { label: 'Export project settings…', run: () => LE.download(store.project + '.settings.json', store.exportJson('project'), 'application/json') }, { label: 'Export editor preferences…', run: () => LE.download('editor-preferences.json', store.exportJson('editor'), 'application/json') }, '-', { label: 'Open the published scene in the player', run: () => open('./#' + (E.scene || ''), '_blank') }, { label: 'Save a screenshot', cmd: 'capture' }],
    Edit: () => [{ label: 'Undo', run: () => { P.script.ta.focus(); document.execCommand('undo'); }, key: 'Ctrl+Z' }, { label: 'Redo', run: () => { P.script.ta.focus(); document.execCommand('redo'); }, key: 'Ctrl+Shift+Z' }, '-', { label: 'Find in script', cmd: 'find', key: 'Ctrl+F' }, { label: 'Command palette', cmd: 'palette', key: 'Ctrl+P' }, '-', { label: 'Project Settings…', cmd: 'projectSettings', key: 'Ctrl+,' }, { label: 'Editor Preferences…', cmd: 'editorPrefs' }],
    View: () => [{ header: 'Overlays' }, ...[['vp.stats', 'Stats'], ['vp.grid', 'Grid'], ['vp.thirds', 'Rule of thirds'], ['vp.safe', 'Safe frames'], ['vp.crosshair', 'Centre mark']].map(([id, l]) => ({ label: l, check: () => store.get(id), run: () => store.set(id, !store.get(id)) })), { label: 'Trailer compare', check: () => trailerOn, cmd: 'trailer', key: 'T' }, '-', { header: 'Debug view' }, ...['Final picture', 'Raw light', 'Exposure', 'Bloom', 'Ray work'].map((l, i) => ({ label: l, check: () => store.get('render.view') === i, run: () => store.set('render.view', i) })), '-', { label: 'Toolbar', check: () => store.get('ed.toolbar'), run: () => store.set('ed.toolbar', !store.get('ed.toolbar')) }, { label: 'Status bar', check: () => store.get('ed.statusbar'), run: () => store.set('ed.statusbar', !store.get('ed.statusbar')) }, { label: 'Viewport only', cmd: 'maximize', key: 'Shift+Space' }, { label: 'Full screen', cmd: 'fullscreen', key: 'F11' }],
    Project: () => [{ header: 'Projects' }, ...E.index.projects.map((p) => ({ label: p.title, check: () => store.project === p.name, run: () => openScene(p.start) })), '-', { label: 'Project Settings…', cmd: 'projectSettings' }, { label: 'Project info', run: () => projectInfo() }],
    Play: () => [{ label: 'Play (walk)', cmd: 'play', key: 'F5' }, { label: 'Movie (shots)', cmd: 'movie', key: 'F6' }, { label: 'Pause', check: () => paused, cmd: 'pause', key: 'F7' }, { label: 'Step a frame', cmd: 'step', key: 'F8' }, { label: 'Restart scene', cmd: 'stop', key: 'Shift+F5' }, '-', { header: 'World speed' }, ...[0.25, 0.5, 1, 2, 4].map((s) => ({ label: s + '×', check: () => store.get('script.timeScale') === s, run: () => store.set('script.timeScale', s) })), '-', { header: 'Quality' }, ...QN.map((n, i) => ({ label: n, check: () => +store.get('render.quality') === i, run: () => store.set('render.quality', i) }))],
    Build: () => [{ label: 'Play in the player', cmd: 'playInPlayer' }, { label: 'Save to project (project.json)…', cmd: 'saveProject' }, '-', { label: 'Build the web site…', cmd: 'buildSite' }, { label: 'Render a cinema still…', cmd: 'renderStill' }, '-', { label: 'Benchmark this scene', cmd: 'benchmark' }, { label: 'Build settings…', run: () => LE.settingsWindow('project', 'build.publish') }],
    Tools: () => [{ label: 'Profiler', run: () => dock.show('profiler') }, { label: 'Benchmark this scene', cmd: 'benchmark' }, { label: 'Save a screenshot', cmd: 'capture' }, '-', { label: 'Sound', check: () => !!audioCtx, cmd: 'sound' }, { label: 'Clear console', cmd: 'clearConsole' }, '-', { label: 'Restart viewport', cmd: 'restartViewport' }],
    Window: () => [{ header: 'Panels' }, ...panelsMenu(), '-', { header: 'Layouts' }, ...Object.keys(LE.LAYOUTS).map((n) => ({ label: n[0].toUpperCase() + n.slice(1), check: () => store.get('ed.layout') === n, run: () => { store.set('ed.layout', n); dock.preset(n); } })), '-', { label: 'Reset layout', cmd: 'resetLayout' }],
    Help: () => [{ label: 'Keyboard shortcuts', cmd: 'help', key: 'F1' }, { label: 'Looped Script quick reference', cmd: 'reference' }, { label: 'What works and what\'s next', run: () => wiringReport() }, '-', { label: 'About Loop3D', cmd: 'about' }],
  };
  const menusEl = $('#menus');
  for (const [name, fn] of Object.entries(MENUS)) {
    const b = h('button', { role: 'menuitem', 'aria-haspopup': 'true' }, name);
    b.onclick = () => { const r = b.getBoundingClientRect(); LE.showMenu(fn(), r.left, r.bottom + 2, b); };
    b.onpointerenter = () => { if (document.querySelector('#menus button.open') && !b.classList.contains('open')) b.click(); };
    menusEl.append(b);
  }
  $('#paletteBtn').onclick = () => LE.palette();

  // ------------------------------------------------------------ toolbar
  document.querySelectorAll('#toolbar [data-cmd]').forEach((b) => { b.onclick = () => LE.run(b.dataset.cmd); });
  document.querySelectorAll('#toolbar [data-tool]').forEach((b) => {
    b.onclick = () => {
      tool = b.dataset.tool;
      document.querySelectorAll('#toolbar [data-tool]').forEach((x) => x.classList.toggle('on', x === b));
      if (tool !== 'select') LE.toast('Moving, turning and scaling things in the viewport isn\'t wired yet: it comes with object editing.');
    };
  });
  $('#tbSpace').onclick = () => store.set('gizmo.space', store.get('gizmo.space') === 'world' ? 'local' : 'world');
  $('#tbSnap').onclick = () => store.set('snap.move', !store.get('snap.move'));
  $('#tbSnap').oncontextmenu = (e) => { e.preventDefault(); LE.showMenu(LE.store.items.get('snap.moveStep').options.map((o) => ({ label: o.label + ' m', check: () => store.get('snap.moveStep') === o.value, run: () => store.set('snap.moveStep', o.value) })), e.clientX, e.clientY); };
  $('#tbSnapRot').onclick = () => store.set('snap.rotate', !store.get('snap.rotate'));
  $('#tbSnapRot').oncontextmenu = (e) => { e.preventDefault(); LE.showMenu(LE.store.items.get('snap.rotateStep').options.map((o) => ({ label: o.label + '°', check: () => store.get('snap.rotateStep') === o.value, run: () => store.set('snap.rotateStep', o.value) })), e.clientX, e.clientY); };
  $('#tbQuality').onchange = (e) => { store.set('render.quality', +e.target.value); e.target.blur(); };
  $('#tbView').onchange = (e) => { store.set('render.view', +e.target.value); e.target.blur(); };
  $('#tbSpeed').onchange = (e) => { store.set('script.timeScale', +e.target.value); e.target.blur(); };
  $('#tbTrailer').onclick = () => LE.run('trailer');
  $('#tbSound').onclick = () => LE.run('sound');
  function syncToolbar() {
    $('#tbQuality').value = String(E.quality);
    $('#tbView').value = String(store.get('render.view'));
    const sp = store.get('script.timeScale');
    if (![...$('#tbSpeed').options].some((o) => +o.value === sp)) $('#tbSpeed').append(h('option', { value: sp }, sp + '×'));
    $('#tbSpeed').value = String(sp);
    $('#tbPlay').classList.toggle('on', !E.movie && !paused);
    $('#tbMovie').classList.toggle('on', E.movie && !paused);
    $('#tbPause').classList.toggle('on', paused);
    $('#tbTrailer').classList.toggle('on', trailerOn);
    $('#tbSound').classList.toggle('on', !!audioCtx);
    $('#tbSound').textContent = audioCtx ? '🔊' : '🔈';
    $('#tbSpace').textContent = store.get('gizmo.space') === 'world' ? 'World' : 'Local';
    $('#tbSnap').classList.toggle('on', store.get('snap.move'));
    $('#tbSnapStep').textContent = store.get('snap.moveStep') + ' m';
    $('#tbSnapRot').classList.toggle('on', store.get('snap.rotate'));
    $('#tbSnapRotStep').textContent = store.get('snap.rotateStep') + '°';
    $('#vpWrap').classList.toggle('movie', E.movie);
  }
  E.on('quality', syncToolbar);
  E.on('mode', syncToolbar);
  function togglePause() { paused = !paused; E.setOption('pause', paused ? 1 : 0); syncToolbar(); LE.status(); }

  // ------------------------------------------------------------ status bar
  LE.status = () => {
    const sc = E.index.projects.length ? (E.projectOf(E.scene)?.title || '') + ' › ' + (E.scene || '') : E.scene || '';
    $('#sbScene').textContent = sc + (P.script && P.script.dirty ? ' • edited' : '');
    $('#sbQuality').textContent = QN[E.quality] + (paused ? ' · paused' : '') + (store.get('script.timeScale') !== 1 ? ' · ' + store.get('script.timeScale') + '×' : '');
    // the current script's problems, not every error the console has seen
    const c = { error: P.script ? P.script.problems.length : 0 };
    const st = $('#sbState');
    if (!E.ready) { st.textContent = 'Starting…'; st.className = 'dot'; }
    else if (c.error) { st.textContent = c.error + ' problem' + (c.error > 1 ? 's' : ''); st.className = 'dot bad'; st.onclick = () => dock.show('script'); }
    else { st.textContent = 'Ready'; st.className = 'dot ok'; }
    $('#sbSaved').textContent = LE.saved ? 'Kept ' + LE.saved.toTimeString().slice(0, 5) : '';
  };
  let statsT = 0;
  function startStats() {
    clearInterval(statsT);
    statsT = setInterval(() => {
      if (!E.M || !E.ready) return;
      const s = E.stats();
      $('#sbFps').textContent = (s.fps || 0).toFixed(0) + ' fps';
      $('#sbGpu').textContent = (s.gpu || 0).toFixed(1) + ' ms GPU';
      $('#sbCpu').textContent = (s.cpu || 0).toFixed(1) + ' ms CPU';
      $('#sbRes').textContent = Math.round((s.scale || 1) * 100) + '% res';
      $('#vpStats').textContent = (s.fps || 0).toFixed(0) + ' fps   ' + QN[E.quality] + (paused ? '  PAUSED' : '') + '\nGPU ' + (s.gpu || 0).toFixed(1) + ' ms  CPU ' + (s.cpu || 0).toFixed(1) + ' ms\nres ' + Math.round((s.scale || 1) * 100) + '% × ' + (s.ratio || 1).toFixed(2) + '\n' + s.things + ' things  ' + s.lights + ' lights  ' + s.draws + ' draws\n' + (s.movie ? 'film ' + (s.film || 0).toFixed(1) + ' s' : 'game') + '  t ' + (s.time || 0).toFixed(1) + ' s';
      const log = E.log();
      if (log) for (const l of log.trim().split('\n')) if (l.trim()) LE.log(l.startsWith('problems') || /line \d+/.test(l) ? 'error' : 'engine', l);
    }, store.get('vp.statsEvery'));
  }

  // ------------------------------------------------------------ viewport
  const vp = $('#vpWrap'), view = $('#view'), note = $('#vpNote');
  const showNote = (t, spin) => { note.textContent = t; note.classList.toggle('loading', !!spin); note.style.display = 'block'; };
  const vpFocus = (on) => vp.classList.toggle('focus', on);
  let focused = false;
  addEventListener('pointerdown', (e) => { focused = vp.contains(e.target); vpFocus(focused); }, true);
  const keyMap = { KeyW: 0, ArrowUp: 0, KeyS: 1, ArrowDown: 1, KeyA: 2, ArrowLeft: 2, KeyD: 3, ArrowRight: 3, ShiftLeft: 4, ShiftRight: 4 };
  const bound = () => (E.M ? LoopUI.keyMap(E.M) : keyMap);
  const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement || e.target instanceof HTMLSelectElement || e.target.isContentEditable;
  addEventListener('keydown', (e) => {
    const mod = e.ctrlKey || e.metaKey;
    if (mod && (e.key === 'p' || e.key === 'k')) { e.preventDefault(); return LE.palette(); }
    if (mod && e.key === ',') { e.preventDefault(); return LE.run('projectSettings'); }
    if (mod && e.key === 'o') { e.preventDefault(); return LE.palette(''); }
    if (mod && e.key === 'n') { e.preventDefault(); return LE.run('newScene'); }
    if (mod && e.key === 'Enter') { e.preventDefault(); return LE.run('runScript'); }
    if (e.key === 'F1') { e.preventDefault(); return LE.run('help'); }
    if (e.key === 'F5') { e.preventDefault(); return LE.run(e.shiftKey ? 'stop' : 'play'); }
    if (e.key === 'F6') { e.preventDefault(); return LE.run('movie'); }
    if (e.key === 'F7') { e.preventDefault(); return LE.run('pause'); }
    if (e.key === 'F8') { e.preventDefault(); return LE.run('step'); }
    if (typing(e) || document.querySelector('.modal-back')) return;
    if (e.code === 'Space' && e.shiftKey) { e.preventDefault(); return LE.run('maximize'); }
    const needFocus = store.get('vp.keysNeedFocus');
    const m = bound();
    if (e.code in m && (focused || !needFocus)) { e.preventDefault(); E.key(m[e.code], true); if (E.movie && m[e.code] < 4) E.setMode(false); return; }
    if (E.M && e.code === LoopUI.useKey(E.M) && !e.repeat && (focused || !needFocus)) return E.useKey();
    if (!focused) {
      if (e.key === 't' || e.key === 'T') return LE.run('trailer');
      const tools = { q: 'select', w: 'move', e: 'rotate', r: 'scale' };
      if (tools[e.key]) document.querySelector('#toolbar [data-tool="' + tools[e.key] + '"]').click();
    }
  });
  addEventListener('keyup', (e) => { const m = bound(); if (e.code in m) E.key(m[e.code], false); });
  addEventListener('blur', () => { for (let i = 0; i < 5; i++) E.key(i, false); });
  let dragging = false, lx = 0, ly = 0;
  view.addEventListener('contextmenu', (e) => e.preventDefault());
  view.addEventListener('pointerdown', (e) => {
    if (e.pointerType !== 'mouse') return;
    if (store.get('vp.drag') === 'right' && e.button !== 2) return;
    dragging = true; lx = e.clientX; ly = e.clientY;
    view.setPointerCapture(e.pointerId);
  });
  view.addEventListener('pointerup', () => { dragging = false; });
  view.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    if (E.movie) E.setMode(false);
    E.look(e.clientX - lx, e.clientY - ly);  // player.invertY is the engine's
    lx = e.clientX; ly = e.clientY;
  });
  function stick(el, onMove, isLook) {
    let id = null, cx = 0, cy = 0, px = 0, py = 0;
    const knob = el.querySelector('i');
    el.addEventListener('touchstart', (e) => { const t = e.changedTouches[0]; id = t.identifier; const r = el.getBoundingClientRect(); cx = r.left + r.width / 2; cy = r.top + r.height / 2; px = t.clientX; py = t.clientY; e.preventDefault(); }, { passive: false });
    el.addEventListener('touchmove', (e) => {
      for (const t of e.changedTouches) if (t.identifier === id) {
        let dx = t.clientX - cx, dy = t.clientY - cy;
        const d = Math.hypot(dx, dy), m = 50;
        if (d > m) { dx *= m / d; dy *= m / d; }
        knob.style.transform = `translate(${dx}px, ${dy}px)`;
        if (isLook) { onMove(t.clientX - px, t.clientY - py); px = t.clientX; py = t.clientY; } else onMove(dx / m, -dy / m);
      }
      e.preventDefault();
    }, { passive: false });
    const end = (e) => { for (const t of e.changedTouches) if (t.identifier === id) { id = null; knob.style.transform = ''; if (!isLook) onMove(0, 0); } };
    el.addEventListener('touchend', end); el.addEventListener('touchcancel', end);
  }
  stick($('#ls'), (x, y) => E.move(x, y), false);
  stick($('#rs'), (dx, dy) => { const k = LoopUI.setting(E.M, 'player.touchLook', 1.6); E.look(dx * k, dy * k); }, true);

  // overlays: grid, thirds, safe frames, centre mark
  const ov = $('#overlay');
  function drawOverlay() {
    const dpr = devicePixelRatio || 1, W = ov.clientWidth * dpr, H = ov.clientHeight * dpr;
    if (!W || !H) return;
    ov.width = W; ov.height = H;
    const x = ov.getContext('2d');
    x.clearRect(0, 0, W, H);
    x.lineWidth = dpr;
    if (store.get('vp.grid')) { x.strokeStyle = '#ffffff22'; const s = 48 * dpr; for (let i = s; i < W; i += s) { x.beginPath(); x.moveTo(i, 0); x.lineTo(i, H); x.stroke(); } for (let j = s; j < H; j += s) { x.beginPath(); x.moveTo(0, j); x.lineTo(W, j); x.stroke(); } }
    if (store.get('vp.thirds')) { x.strokeStyle = '#ffffff66'; for (const f of [1 / 3, 2 / 3]) { x.beginPath(); x.moveTo(W * f, 0); x.lineTo(W * f, H); x.moveTo(0, H * f); x.lineTo(W, H * f); x.stroke(); } }
    if (store.get('vp.safe')) { x.strokeStyle = '#f0b54499'; x.setLineDash([6 * dpr, 4 * dpr]); x.strokeRect(W * 0.05, H * 0.05, W * 0.9, H * 0.9); x.strokeStyle = '#4c9dff99'; x.strokeRect(W * 0.1, H * 0.1, W * 0.8, H * 0.8); x.setLineDash([]); }
    // light icons and bounding boxes, placed with the engine's own view of the scene
    const info = P.outliner && P.outliner.info;
    if (info && info.viewProj && (store.get('render.showLights') || store.get('render.showBounds'))) {
      const m = info.viewProj;
      const proj = (p) => {
        const x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        return w > 0.05 ? [(x / w * 0.5 + 0.5) * W, (0.5 - y / w * 0.5) * H] : null;
      };
      if (store.get('render.showBounds')) {
        x.strokeStyle = '#4cc38acc';
        for (const t of info.things.slice(0, 800)) {
          if (!t.visible || !t.size) continue;
          const c = t.position, hs = t.size.map((v) => Math.abs(v) / 2);
          const pts = [];
          for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) pts.push(proj([c[0] + sx * hs[0], c[1] + sy * hs[1], c[2] + sz * hs[2]]));
          if (pts.some((q) => !q)) continue;
          for (const [a, b] of [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]]) { x.beginPath(); x.moveTo(...pts[a]); x.lineTo(...pts[b]); x.stroke(); }
        }
      }
      if (store.get('render.showLights')) {
        x.font = 14 * dpr + 'px system-ui';
        x.textAlign = 'center'; x.textBaseline = 'middle';
        for (const l of info.lights) {
          if (l.kind === 'sun') continue;
          const q = proj(l.position);
          if (!q) continue;
          x.fillStyle = '#000a'; x.beginPath(); x.arc(q[0], q[1], 11 * dpr, 0, Math.PI * 2); x.fill();
          x.fillStyle = '#ffd27a'; x.fillText(l.kind === 'spot' ? '◒' : l.kind === 'rect' ? '▭' : '💡', q[0], q[1]);
        }
      }
    }
    if (store.get('vp.crosshair')) { x.strokeStyle = '#ffffffaa'; const c = 10 * dpr; x.beginPath(); x.moveTo(W / 2 - c, H / 2); x.lineTo(W / 2 + c, H / 2); x.moveTo(W / 2, H / 2 - c); x.lineTo(W / 2, H / 2 + c); x.stroke(); }
  }
  new ResizeObserver(() => { drawOverlay(); P.script && P.script.paint(); }).observe(vp);
  // the overlay follows the camera while light icons or boxes are on
  setInterval(() => { if (E.M && (store.get('render.showLights') || store.get('render.showBounds'))) { P.outliner.refresh(true); drawOverlay(); } }, 250);
  LoopUI.gamepad(() => E.M, () => E.useKey(), () => { if (E.movie) E.setMode(false); });
  LoopUI.pauseWhenHidden(() => E.M);
  setInterval(() => E.M && LoopUI.applyPage(E.M), 1000);

  // trailer compare: the cinema still over the live view, split by a handle
  const timg = $('#trailerImg'), tsplit = $('#trailerSplit');
  function setTrailer(on) {
    if (on && !timg.dataset.ok) { LE.toast('No cinema still for this scene yet.'); on = false; }
    trailerOn = on;
    vp.classList.toggle('trailer', on);
    placeSplit();
    syncToolbar();
  }
  function placeSplit() { timg.style.clipPath = 'inset(0 0 0 ' + split * 100 + '%)'; tsplit.style.left = split * 100 + '%'; }
  tsplit.addEventListener('pointerdown', (e) => {
    tsplit.setPointerCapture(e.pointerId);
    const mv = (ev) => { const r = vp.getBoundingClientRect(); split = Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width)); placeSplit(); };
    const up = () => { tsplit.removeEventListener('pointermove', mv); tsplit.removeEventListener('pointerup', up); };
    tsplit.addEventListener('pointermove', mv); tsplit.addEventListener('pointerup', up);
  });
  timg.onload = () => { timg.dataset.ok = '1'; };
  timg.onerror = () => { delete timg.dataset.ok; if (trailerOn) setTrailer(false); };

  // dialogue and choices
  const linesBox = $('#vpLines'), askBox = $('#vpAsk');
  E.on('ui', (m) => {
    if (m.kind === 'clear') { linesBox.innerHTML = ''; return; }
    if (m.kind === 'line') {
      LE.log('line', (m.speaker ? m.speaker + ': ' : '') + m.text);
      LoopUI.showLine(linesBox, E.M, m);
      return;
    }
    if (m.kind === 'ask') {
      LE.log('line', '? ' + (m.text || '') + ' [' + m.options.join(' / ') + ']');
      // autoplay (tests) answers by itself, from script.choices
      if (LoopUI.setting(E.M, 'script.autoplay', false)) {
        const list = String(LoopUI.setting(E.M, 'script.choices', '0')).split(',').map(Number).filter((x) => !isNaN(x));
        const pick = list.length ? list[(autoAsked++) % list.length] : 0;
        setTimeout(() => E.choose(Math.min(pick, m.options.length - 1)), 1200);
        return;
      }
      LoopUI.showAsk(askBox, E.M, m, (i) => E.choose(i));
    }
  });

  // ------------------------------------------------------------ engine events
  E.on('nogpu', () => { if (E.ready) { showNote('The graphics card stopped answering twice: this scene may be too heavy for it. Try Phone quality, or lower Forest density and Grass in Project Settings → World, then reload.', false); LE.log('error', 'The GPU was lost and could not be got back.'); LE.status(); return; } showNote('This browser can\'t do WebGPU yet. On iPhone: Safari 26 or newer; on a computer: Chrome, Edge or Safari. The editor still works; the viewport needs WebGPU.', false); LE.log('error', 'No WebGPU in this browser.'); LE.status(); });
  E.on('loading', (name) => { const s = E.projectOf(name)?.scenes.find((x) => x.name === name); showNote(LoopUI.loadingText(E.M, s ? s.title : name || 'the scene'), true); linesBox.innerHTML = ''; askBox.style.display = 'none'; if (store.get('con.clearOnLoad')) LE.run('clearConsole'); });
  E.on('shown', () => { note.style.display = 'none'; LE.status(); });
  E.on('problems', (t) => { P.script.pendingProblems = true; for (const l of t.trim().split('\n')) LE.log('error', l); dock.show('script'); LE.status(); });
  E.on('gpulost', () => { showNote('The graphics card restarted. Reconnecting…', true); LE.log('warn', 'GPU lost: reconnecting'); });
  E.on('gpuback', (q) => { note.style.display = 'none'; LE.log('warn', 'GPU back on ' + QN[q]); if (q !== +store.get('render.quality')) LE.toast('Switched to ' + QN[q] + ' so it keeps running smoothly'); });
  E.on('log', (kind, t) => LE.log(kind, t));
  E.on('started', (s) => { LE.log('engine', 'Started "' + s.title + '" · ' + s.mode + (s.shots ? ' · ' + s.shots + ' shots' : '')); syncToolbar(); LE.status(); if (paused) E.setOption('pause', 1); });
  E.on('goto', (name) => LE.log('engine', 'go_to → ' + name));
  E.on('sceneText', (name) => {
    const p = E.projectOf(name);
    if (p && p.name !== store.project) setProject(p);
    timg.removeAttribute('data-ok');
    timg.src = 'scenes/' + encodeURIComponent(name) + '.png';
    if (location.hash.slice(1) !== name) history.replaceState(null, '', '#' + name);
    document.title = (p ? p.title + ' › ' : '') + name + ' · Loop3D Editor';
    LE.status();
  });
  E.textFor = (name, text) => (P.script.name === name ? P.script.ta.value : text);
  E.on('ready', async () => {
    LE.log('engine', 'Engine ready · WebGPU · ' + QN[E.quality] + (E.has('loop_set_option') ? '' : ' · old player build: settings that need loop_set_option won\'t apply'));
    applyAllEngine();
    syncToolbar();
    startStats();
    const first = (location.hash.slice(1) || params.get('scene') || '').replace(/[^a-z0-9_-]/gi, '') || E.index.projects[0]?.start || 'courtyard';
    try { await openScene(first); } catch (e) { showNote('Couldn\'t load the scene "' + first + '".', false); LE.log('error', String(e.message || e)); }
    LE.status();
  });

  async function openScene(name) {
    if (P.script.dirty && P.script.name && P.script.name !== name) LE.safe.set('loop3d.script.' + P.script.name, P.script.ta.value);
    try { await E.loadScene(name); } catch (e) { LE.toast('Couldn\'t open ' + name, 'err'); throw e; }
  }
  function setProject(p) {
    store.project = p.name;
    store.load('project');
    // the project's own values (project.json) are its settings' starting points: Reset goes back to them
    for (const it of store.items.values()) if (it.scope === 'project' && it.origDef === undefined) it.origDef = it.def;
    for (const it of store.items.values()) if (it.scope === 'project') it.def = it.origDef;
    for (const [id, v] of [['project.title', p.title], ['project.description', p.description], ['project.start', p.start]]) store.items.get(id).def = v;
    for (const [id, v] of Object.entries(p.settings || {})) if (store.items.has(id)) store.items.get(id).def = v;
    for (const [k, v] of Object.entries(p.build || {})) for (const id of ['build.' + k, 'project.' + k]) if (store.items.has(id)) store.items.get(id).def = v;
    E.projectTitle = p.title;
    $('#projectChip').textContent = p.title;
    $('#projectChip').title = p.description;
    if (E.ready) applyAllEngine();
    LE.panels.look.draw && LE.panels.look.draw();
  }
  function newScene() {
    const name = (prompt('Name for the new scene (letters, numbers and _):', 'my_scene') || '').trim().replace(/[^a-z0-9_]/gi, '_');
    if (!name) return;
    const text = '# ' + name + ': a new Loop3D scene\ntitle "' + name.replace(/_/g, ' ') + '"\nmode "game"\n\nsun elevation: 35 deg, toward: 200 deg\nsky "physical", haze: 2.5\nground 60, material: "grass"\n\nlet table = box(size: vec(1.6, 0.05, 0.9), at: vec(0, 0.75, 0), material: "oak")\nsphere radius: 0.15, at: vec(0, 0.92, 0), material: "chrome"\nlight at: vec(0, 2.2, 0), lumens: 800, kelvin: 2700 K\n\ncamera at: vec(0, 1.6, 4), look_at: vec(0, 0.9, 0)\nlook "natural"\n';
    E.scene = name;
    P.script.load(name, text, false);
    P.script.dirty = true;
    dock.show('script');
    P.script.run();
    LE.toast('New scene ' + name + '.loop · Save .loop to keep the file');
  }
  function restartViewport() {
    const u = new URLSearchParams(location.search);
    for (const it of store.items.values()) if (it.bind && it.bind.startsWith('restart:')) { const k = it.bind.slice(8); if (store.isModified(it.id)) u.set(k, store.get(it.id)); else u.delete(k); }
    if (P.script.name) LE.safe.set('loop3d.script.' + P.script.name, P.script.ta.value);
    location.href = location.pathname + (u.toString() ? '?' + u : '') + '#' + (E.scene || '');
  }

  // ------------------------------------------------------------ sound, capture, benchmark
  function toggleSound() {
    if (!E.M) return;
    if (audioCtx) { audioCtx.close(); audioCtx = null; syncToolbar(); return; }
    audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 48000 });
    const frames = +LoopUI.setting(E.M, 'audio.buffer', 2048), node = audioCtx.createScriptProcessor(frames, 0, 2), ptr = E.M._malloc(frames * 8);
    node.onaudioprocess = (e) => {
      E.M._loop_audio(ptr, frames);
      const buf = E.M.HEAPF32.subarray(ptr >> 2, (ptr >> 2) + frames * 2), L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
      for (let i = 0; i < frames; i++) { L[i] = buf[i * 2]; R[i] = buf[i * 2 + 1]; }
    };
    node.connect(audioCtx.destination);
    syncToolbar();
  }
  async function capture() {
    if (!E.M) return;
    const { w, h: hh, px } = await E.capture();
    const c = document.createElement('canvas'); c.width = w; c.height = hh;
    c.getContext('2d').putImageData(new ImageData(px, w, hh), 0, 0);
    c.toBlob((b) => LE.download((E.scene || 'loop3d') + '-' + new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-') + '.png', b));
    LE.toast('Screenshot saved');
  }
  let benching = false;
  async function benchmark() {
    if (!E.M || benching) return;
    benching = true;
    const secs = store.get('perf.benchSeconds');
    dock.show('profiler');
    LE.log('cmd', 'Benchmark ' + E.scene + ' on ' + QN[E.quality] + ' for ' + secs + ' s');
    E.setAutoplay(true);
    E.perfBegin();
    await P.script.run();
    const t0 = performance.now();
    const tick = setInterval(() => { showNote('Benchmark ' + QN[E.quality] + ' · ' + Math.round((performance.now() - t0) / 1000) + ' / ' + secs + ' s', true); }, 500);
    await new Promise((r) => setTimeout(r, secs * 1000));
    clearInterval(tick);
    note.style.display = 'none';
    const r = E.perfEnd();
    E.setAutoplay(!!LoopUI.setting(E.M, 'script.autoplay', false));
    r.scene = E.scene; r.quality = QN[E.quality];
    P.profiler.addRun(r);
    LE.log('engine', 'Benchmark: ' + r.fps.toFixed(1) + ' fps · 99% ' + r.p99Ms.toFixed(1) + ' ms · ' + r.hitches + ' hitches · GPU ' + r.gpuMs.toFixed(1) + ' ms');
    benching = false;
  }

  // ------------------------------------------------------------ console commands
  LE.consoleCommand = (line) => {
    LE.log('cmd', '› ' + line);
    const [cmd, ...rest] = line.split(/\s+/);
    const arg = rest.join(' ');
    const say = (t) => LE.log('info', t);
    switch (cmd.toLowerCase()) {
      case 'help': say('help · scene <name> · scenes · quality 0|1|2 · look <name>|off · set <setting> <value> · get <setting> · run · pause · step · speed <x> · movie · play · bench · shot · clear · settings\nsettings use the names in Project Settings (hover a label), e.g. set physics.iterations 24'); break;
      case 'scene': case 'open': openScene(arg).catch(() => LE.log('error', 'No scene "' + arg + '"')); break;
      case 'scenes': for (const p of E.index.projects) say(p.title + ': ' + p.scenes.map((s) => s.name).join(', ')); break;
      case 'quality': store.set('render.quality', Math.max(0, Math.min(2, +arg || 0))); break;
      case 'look': store.set('look.preset', arg === 'off' ? '' : arg); break;
      case 'set': {
        const [k, ...vs] = rest;
        let v = vs.join(' ');
        try { v = JSON.parse(v); } catch (e) { /* plain text */ }
        if (store.items.has(k)) { store.set(k, v); say(k + ' = ' + JSON.stringify(store.get(k))); }
        else say(E.setSetting(k, v) || E.setOption(k, +v) ? k + ' = ' + JSON.stringify(v) : 'no setting ' + k + ' (see the console for a suggestion)');
        break;
      }
      case 'get': { const v = E.getSetting(arg); say(v !== undefined && v !== null ? arg + ' = ' + JSON.stringify(v) + ' (in effect)' : store.items.has(arg) ? arg + ' = ' + JSON.stringify(store.get(arg)) : 'no setting ' + arg); break; }
      case 'run': P.script.run(); break;
      case 'pause': LE.run('pause'); break;
      case 'step': E.setOption('step', +arg || 1); break;
      case 'speed': store.set('script.timeScale', +arg || 1); break;
      case 'movie': LE.run('movie'); break;
      case 'play': LE.run('play'); break;
      case 'bench': benchmark(); break;
      case 'shot': capture(); break;
      case 'clear': LE.run('clearConsole'); break;
      case 'settings': LE.settingsWindow('project'); break;
      case 'info': say(JSON.stringify(E.stats())); break;
      default: LE.log('warn', 'Unknown command "' + cmd + '". Type help.');
    }
  };

  // ------------------------------------------------------------ help windows
  function shortcuts() {
    const rows = [['Ctrl+P / Ctrl+K', 'Command palette (commands, scenes, settings)'], ['Ctrl+,', 'Project Settings'], ['Ctrl+Enter', 'Run the script'], ['Ctrl+S', 'Keep and run (Run on Ctrl+S)'], ['Ctrl+Shift+S', 'Download the .loop file'], ['Ctrl+/', 'Comment lines'], ['Tab / Shift+Tab', 'Indent / outdent'], ['Ctrl+F', 'Find in script'], ['F5 / Shift+F5', 'Play (walk) / restart the scene'], ['F6', 'Movie (play the shots)'], ['F7 / F8', 'Pause / step one frame'], ['W A S D, arrows', 'Walk (click the view first)'], ['Shift', 'Run'], ['E', 'Use / talk'], ['Drag in the view', 'Look around'], ['T', 'Trailer compare'], ['Q W E R', 'Select, move, turn, scale tools (outside the view)'], ['Shift+Space', 'Viewport only'], ['F11', 'Full screen'], ['Right-click a tab', 'Move it to another dock'], ['Drag a tab', 'Dock it somewhere else']];
    LE.modal({ title: 'Keyboard shortcuts', cls: 'small', body: h('div', { class: 'content' }, h('table', { class: 'help-table' }, rows.map(([k, d]) => h('tr', {}, h('td', {}, h('kbd', {}, k)), h('td', {}, d))))) });
  }
  function reference() {
    LE.info('Looped Script quick reference', '<div class="ref">' +
      '<h4>Basics</h4><p><code>let x = 2 m</code> · <code>to greet(who)</code> · <code>if / else / otherwise</code> · <code>for each</code> · <code>repeat 3 times</code> · comments start with <code>#</code>. Numbers carry units: <code>30 cm</code>, <code>2 s</code>, <code>45 deg</code>, <code>2700 K</code>, <code>60%</code>.</p>' +
      '<h4>Time</h4><p><code>when "use"</code> · <code>every 2 s</code> · <code>after 1 s</code> · <code>wait 3 s</code> · <code>task</code> runs things at once.</p>' +
      '<h4>World</h4><p><code>box sphere cylinder cone capsule ring ground rock tree terrain forest</code>; every shape takes <code>at turn scale material name</code>. <code>material("oak")</code> or <code>material(like: "oak", roughness: 0.3)</code>.</p>' +
      '<h4>Light</h4><p><code>light(at, lumens, kelvin)</code> · <code>panel_light</code> · <code>sun(elevation, toward, lux)</code> · <code>sky("physical", haze, stars)</code> · <code>fog(density, top)</code></p>' +
      '<h4>Camera, look, shots</h4><p><code>camera(at, look_at, lens, f_stop)</code> · <code>look("noir", bloom, grain, bars)</code> · <code>shot(name, length, from, to)</code> · <code>orbit(...)</code></p>' +
      '<h4>Life</h4><p><code>solid(shape, mass, bounce)</code> · <code>push</code> · <code>person(at)</code> with <code>.walk_to .look_at .sitting</code> · <code>sound("fire", at)</code> · <code>room(size, echo)</code></p>' +
      '<h4>Story</h4><p><code>line(speaker, text)</code> · <code>ask(question, [options])</code> → <code>when "choice"</code> · <code>go_to("next")</code> · <code>remember / recall</code> · <code>use "common"</code></p>' +
      '<p class="dim">The full reference is docs/LANGUAGE.md in the repository.</p></div>');
  }
  function about() {
    const build = document.querySelector('meta[name="loop-build"]').content;
    LE.info('About Loop3D', '<p><b>Loop3D Editor</b> · build ' + build + '</p><p>J Production\'s engine, written from scratch in C++ with no outside engines or libraries. The live view is WebGPU; the cinema renderer is a path tracer.</p><p class="dim">Projects are folders in <code>projects/</code>; the engine doesn\'t need any of them.</p>');
  }
  function projectInfo() {
    const p = E.projectOf(E.scene);
    if (!p) return;
    LE.info(p.title, '<p>' + p.description + '</p><p><b>' + p.scenes.length + '</b> scenes · start: <code>' + p.start + '</code>' + (p.library?.length ? ' · shared: ' + p.library.map((l) => '<code>' + l + '</code>').join(', ') : '') + '</p><p class="dim">Folder: projects/' + p.name + '/ · settings saved in this browser as ' + store.key('project') + ' (Export to keep them with the project).</p>');
  }
  function wiringReport() {
    const all = [...store.items.values()];
    const st = (it) => store.status(it);
    const count = (k) => all.filter((i) => st(i) === k).length;
    const row = (cat) => {
      const its = cat.groups.flatMap((g) => g.items);
      const ok = its.filter((i) => st(i) === 'live' || st(i) === 'build').length;
      const nb = its.filter((i) => st(i) === 'nb');
      return '<tr><td>' + cat.icon + ' ' + cat.title + '</td><td>' + ok + ' / ' + its.length + '</td><td class="dim">' + nb.slice(0, 4).map((i) => i.label).join(', ') + (nb.length > 4 ? '…' : '') + '</td></tr>';
    };
    LE.info('What works and what\'s next', '<p><b>' + (count('live') + count('build')) + '</b> of ' + all.length + ' settings work today: they change the engine (or the editor, or the site build) as you set them, and a scene can set the engine ones itself with <code>setting "name", value</code>. <span class="badge nw">Not built yet</span> marks ' + count('nb') + ' settings for features the engine doesn\'t have yet (the roadmap); <span class="badge rs">Needs you</span> marks ' + count('you') + ' that are your call.</p><table class="data"><tr><th>Section</th><th>Working</th><th>Not built yet</th></tr>' + LE.SCHEMA.project.map(row).join('') + LE.SCHEMA.editor.map(row).join('') + '</table><p class="dim">Every setting, its script word and its range: docs/SETTINGS.md.</p>');
  }

  // "Play": the player page, on exactly what's open here (edits and all)
  function playInPlayer() {
    if (!E.scene) return LE.toast('Open a scene first');
    if (!LoopUI.handTo(E.scene, P.script.ta.value, store.project)) return LE.toast('This browser won\'t let the editor hand the scene over (private mode?)', 'err');
    open('./?from=editor#' + encodeURIComponent(E.scene), '_blank');
  }
  // "Save to project": project.json with the settings changed here
  function saveProject() {
    const p = E.index.projects.find((x) => x.name === store.project);
    if (!p) return LE.toast('No project open', 'err');
    const out = { name: p.name, title: store.get('project.title') || p.title, description: store.get('project.description') || p.description, start: store.get('project.start') || p.start,
      scenes: p.scenes.map((s) => s.name) };
    if (p.library && p.library.length) out.library = p.library;
    out.stills = store.get('project.stillsDir') || 'stills';
    if (p.order !== undefined) out.order = p.order;
    const settings = { ...(p.settings || {}) }, build = { ...(p.build || {}) };
    for (const it of store.items.values()) {
      if (it.scope !== 'project' || !store.isModified(it.id)) continue;
      // engine settings go to project.json; the look, fog, sky and camera overrides are for
      // trying things live (a scene sets its own with look(…), sky(…), fog(…), camera(…))
      if (it.bind === 'set') settings[it.id] = store.get(it.id);
      else if (it.bind === 'build') build[it.id.replace(/^(build|project)\./, '')] = store.get(it.id);
    }
    if (Object.keys(settings).length) out.settings = settings;
    if (Object.keys(build).length) out.build = build;
    LE.download('project.json', JSON.stringify(out, null, 2) + '\n', 'application/json');
    LE.info('Save to project', '<p>Downloaded <code>project.json</code> for <b>' + (out.title || p.name) + '</b>, with ' + Object.keys(settings).length + ' settings.</p><p>Put it in <code>projects/' + p.name + '/</code> in the repository (replacing the old one). The player, the editor, <code>loop render</code> and the site build all read it.</p>');
  }

  // ------------------------------------------------------------ start
  store.load('editor');
  applyEditor();
  LE.initPanels();
  dock.load();
  dock.render();
  matchMedia('(max-width: 760px)').addEventListener('change', () => dock.render());
  LE.emitLayout = () => { drawOverlay(); };
  syncToolbar();
  LE.status();
  E.loadIndex().then(() => {
    const first = (location.hash.slice(1) || '').replace(/[^a-z0-9_-]/gi, '');
    const p = E.projectOf(first) || E.index.projects[0];
    if (p) setProject(p);
    if (!booted) { booted = true; E.boot(); }
  });
  addEventListener('hashchange', () => { const n = location.hash.slice(1); if (n && n !== E.scene && E.ready) openScene(n).catch(() => {}); });
})();
