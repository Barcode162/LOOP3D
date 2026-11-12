// The Loop3D editor: starts the engine, opens scenes (their kept text over
// the file), and joins the world view, the four spaces, undo and redo, Play,
// the menu, keys and sound. docs/EDITOR.md says why it is the way it is.
(() => {
  const { h, store } = LE;
  const E = LE.engine, H = LE.history, V = LE.view, SP = LE.spaces, S = LE.source;
  const $ = (s) => document.querySelector(s);
  const params = new URLSearchParams(location.search);
  const touch = matchMedia('(pointer: coarse)').matches;
  if (touch) document.body.classList.add('touch');
  const QN = ['Phone', 'High', 'Ultra'];
  const note = $('#vpNote');
  const showNote = (t, spin) => { note.textContent = t; note.classList.toggle('loading', !!spin); note.style.display = 'block'; };

  // icons on the frame's buttons
  $('#menuBtn').append(LE.icon('menu'));
  $('#undoBtn').append(LE.icon('undo'));
  $('#redoBtn').append(LE.icon('redo'));
  $('#playBtn').prepend(LE.icon('play'));

  // ---------------------------------------------------------------- settings → engine (Project Settings, Editor Preferences)
  let lookT = 0, worldT = 0;
  const pushBound = (prefix, send, t) => { clearTimeout(t); return setTimeout(() => { const o = {}; for (const it of store.items.values()) if (it.bind && it.bind.startsWith(prefix) && store.isModified(it.id)) o[it.bind.slice(prefix.length)] = store.get(it.id); if (E.M) send(o); }, 16); };
  // the film's black bars belong to the film: while building or playing the world gets the whole view
  const sendLook = (o) => E.setLook(V.mode === 'film' ? o : { ...o, letterbox: 0 });
  function applyEngine(it) {
    if (!E.M || !it.bind) return;
    const b = it.bind;
    if (b === 'quality') E.setQuality(+store.get(it.id));
    else if (b === 'set') E.setSetting(it.id, store.isModified(it.id) ? store.get(it.id) : null);
    else if (b.startsWith('look:')) lookT = pushBound('look:', sendLook, lookT);
    else if (b.startsWith('world:')) worldT = pushBound('world:', E.setWorld, worldT);
  }
  function applyAll() {
    const proj = E.projectOf(E.scene);
    if (proj && E.has('loop_set_project_settings')) E.setProjectSettings(proj.settings || {});
    for (const it of store.items.values()) {
      if (it.scope !== 'project' || !it.bind) continue;
      if (it.bind === 'quality') { if (store.isModified(it.id) || params.get('quality')) E.setQuality(+(params.get('quality') ?? store.get(it.id))); continue; }
      if (it.bind === 'set' && store.isModified(it.id)) E.setSetting(it.id, store.get(it.id));
    }
    lookT = pushBound('look:', sendLook, lookT);
    worldT = pushBound('world:', E.setWorld, worldT);
  }
  V.on((kind) => { if (kind === 'mode') lookT = pushBound('look:', sendLook, lookT); });
  // the editor's own preferences (Editor preferences)
  function applyPref(id) {
    const g = (k) => store.get(k), root = document.documentElement.style;
    if (!id || id === 'ed.accent') root.setProperty('--accent', g('ed.accent'));
    if (!id || id === 'ed.fontSize') root.setProperty('--fs', g('ed.fontSize') + 'px');
    if (!id || id === 'code.fontSize') root.setProperty('--code-fs', g('code.fontSize') + 'px');
    if (!id || id === 'vp.stats') $('#speed').style.display = g('vp.stats') ? '' : 'none';
    if (!id || id === 'snap.move') V.snap = g('snap.move');
    if (!id || id === 'snap.moveStep') V.moveStep = +g('snap.moveStep');
    if (!id || id === 'snap.rotateStep') V.turnStep = +g('snap.rotateStep') * Math.PI / 180;
    if (!id || id === 'vp.lens') { V.cam.lens = g('vp.lens'); if (id) V.pushCamera(); }
    if (!id || id === 'vp.flySpeed') V.flySpeed = g('vp.flySpeed');
    if (!id || id === 'code.live') LE.logic.live = g('code.live');
    if (!id || id === 'code.autocomplete') LE.logic.autocomplete = g('code.autocomplete');
    if (id === 'ed.workLight') V.setWorkLight(g('ed.workLight') === 'on' || (g('ed.workLight') === 'auto' && SP.info && !SP.info.lights.length));
    if (id === 'render.showLights') { SP.marks(); SP.redraw(); }
    if (id) SP.redraw();
  }
  store.load('editor');
  applyPref();
  store.on((id, v, it) => { if (it.bind === 'editor') applyPref(id); else applyEngine(it); });

  // ---------------------------------------------------------------- scenes
  let firstStart = true, problemsSeen = false;
  async function openScene(name) {
    showNote(LoopUI.loadingText(E.M, titleOf(name)), true);
    let fileText;
    try { fileText = await E.fetchScene(name); } catch (e) { showNote('There\'s no scene called "' + name + '".', false); return; }
    const proj = E.projectOf(name);
    store.project = proj ? proj.name : 'default';
    store.load('project');
    E.scene = name;
    E.projectTitle = proj ? proj.title : '';
    $('#sceneTitle').textContent = titleOf(name);
    $('#projTitle').textContent = (proj ? proj.title + ' · ' : '') + name + '.loop';
    document.title = titleOf(name) + ' · Loop3D Editor';
    if (location.hash.slice(1) !== name) history.replaceState(null, '', '#' + name);
    const text = await H.open(name, fileText);
    LE.studio.reset();
    firstStart = true;
    V.select(null); V.selMark = null;
    applyAll();
    await E.runText(text, name);
    if (H.edited()) LE.toast('Your changes to ' + name + '.loop are back (Menu → Back to the file drops them)');
  }
  E.loadScene = openScene;  // a script's go_to("…") opens the next scene here too
  const titleOf = (n) => { for (const p of E.index.projects) { const s = p.scenes.find((x) => x.name === n); if (s) return s.title; } return n; };

  E.on('ready', async () => {
    await E.loadIndex();
    E.setQuality(params.get('quality') != null ? +params.get('quality') : touch ? 0 : 1);
    LoopUI.pauseWhenHidden(() => E.M);
    const first = E.index.projects.find((p) => p.scenes.length);
    const name = (location.hash.slice(1) || params.get('scene') || (first ? first.scenes[0].name : 'courtyard')).replace(/[^a-z0-9_-]/gi, '');
    let space = 'build';
    try { space = localStorage.getItem('loop3d.editor.space') || 'build'; } catch (e) { /* private mode */ }
    SP.show(['build', 'logic', 'film', 'sound'].includes(space) ? space : 'build');
    openScene(name);
  });
  E.on('problems', (t) => { problemsSeen = true; LE.logic.setProblems(t); log('error', t.trim()); });
  E.on('started', () => {
    if (!problemsSeen) LE.logic.setProblems('');
    problemsSeen = false;
    const info = SP.refreshInfo();
    if (firstStart && info) {
      firstStart = false;
      V.mode = 'edit';
      document.body.classList.remove('playing');
      V.cameraFrom(info.camera.position, info.camera.target);
      // a world with no lamp and no sun is dark to build in: the work light comes on (Build turns it off)
      const wl = store.get('ed.workLight');
      V.setWorkLight(wl === 'on' || (wl === 'auto' && !info.lights.length));
    } else V.pushCamera();
    if (LE.studio.open) LE.studio.refresh();
    else if (SP.pendingSelect) { V.selectLine(SP.pendingSelect); SP.pendingSelect = null; } else V.reselect();
    SP.marks();
    SP.redraw();
  });
  E.on('shown', () => { note.style.display = 'none'; });
  E.on('loading', () => { if (firstStart) showNote(LoopUI.loadingText(E.M, titleOf(E.scene)), true); });
  E.on('nogpu', () => showNote('This browser can\'t draw with WebGPU yet. On iPhone: Safari 26 or newer; on a computer: Chrome, Edge or Safari.', false));
  E.on('gpulost', () => showNote('The graphics card restarted. Reconnecting…', true));
  E.on('gpuback', () => { note.style.display = 'none'; });
  E.on('ui', (m) => {
    if (m.kind === 'clear') { $('#lines').innerHTML = ''; return; }
    if (m.kind === 'line') LoopUI.showLine($('#lines'), E.M, { speaker: m.speaker, text: m.text, seconds: m.seconds });
    if (m.kind === 'ask') LoopUI.showAsk($('#ask'), E.M, { text: m.text, options: m.options }, (i) => E.choose(i));
  });
  // the engine's messages, for Logic → Messages
  function log(kind, text) { if (!text) return; LE.logic.messages.push({ kind, text }); if (LE.logic.messages.length > 500) LE.logic.messages.shift(); }
  LE.log = log;
  E.on('log', log);
  setInterval(() => { if (E.M) { const t = E.log(); if (t) t.trim().split('\n').forEach((l) => log('info', l)); } }, 1000);

  // ---------------------------------------------------------------- undo, redo, saved
  const undoBtn = $('#undoBtn'), redoBtn = $('#redoBtn'), saved = $('#savedDot');
  let savedT = 0;
  H.on((kind) => {
    undoBtn.disabled = !H.undo.length;
    redoBtn.disabled = !H.redo.length;
    undoBtn.title = H.undo.length ? 'Undo: ' + H.undo[H.undo.length - 1].label + ' (Ctrl+Z)' : 'Nothing to undo';
    redoBtn.title = H.redo.length ? 'Redo: ' + H.redo[H.redo.length - 1].label + ' (Ctrl+Shift+Z)' : 'Nothing to redo';
    if (kind === 'saved') { saved.classList.add('on'); clearTimeout(savedT); savedT = setTimeout(() => saved.classList.remove('on'), 1200); }
  });
  function step(dir) {
    const e = dir === 'undo' ? H.undoOnce() : H.redoOnce();
    if (!e) return;
    LE.toast((dir === 'undo' ? 'Undid: ' : 'Redid: ') + e.label);
    if (e.fast) { V.applyFast(e.fast, dir); LE.logic.sync(); SP.marks(); SP.redraw(); }
    else { LE.logic.sync(); SP.rerun(); }
  }
  undoBtn.onclick = () => step('undo');
  redoBtn.onclick = () => step('redo');

  // ---------------------------------------------------------------- Play
  const playBtn = $('#playBtn');
  V.on((kind, m) => {
    if (kind !== 'mode') return;
    playBtn.querySelector('.lbl').textContent = m === 'play' ? 'Stop' : 'Play';
    playBtn.querySelector('.ico').replaceWith(LE.icon(m === 'play' ? 'pause' : 'play'));
    if (m === 'play') V.hint(touch ? 'Left thumb walks, right thumb looks. Your changes stay.' : 'W A S D walk, drag to look, E uses. Your changes stay.', 3500);
  });
  playBtn.onclick = () => V.setMode(V.mode === 'play' ? 'edit' : 'play');

  // ---------------------------------------------------------------- the sheet's height (phone)
  const sheet = $('#sheet'), grip = $('#grip');
  const HEIGHTS = [0.3, 0.48, 0.8];
  let sheetH = 0.38;
  try { sheetH = +localStorage.getItem('loop3d.editor.sheet') || 0.38; } catch (e) { /* private mode */ }
  const setSheet = (f, settle) => {
    sheetH = Math.max(0.18, Math.min(0.86, f));
    document.documentElement.style.setProperty('--sheet-h', Math.round(sheetH * 100) + 'vh');
    if (settle) document.documentElement.style.setProperty('--stage-sheet', Math.round(sheetH * 100) + 'vh');
  };
  setSheet(sheetH, true);
  grip.addEventListener('pointerdown', (e) => {
    grip.setPointerCapture(e.pointerId);
    sheet.classList.add('dragging');
    const y0 = e.clientY, h0 = sheetH;
    grip.onpointermove = (ev) => setSheet(h0 + (y0 - ev.clientY) / innerHeight);
    grip.onpointerup = () => {
      grip.onpointermove = null; sheet.classList.remove('dragging');
      if (sheetH < 0.2) { sheet.classList.add('closed'); document.body.classList.add('sheet-closed'); document.querySelectorAll('#spaces button').forEach((b) => b.classList.remove('on')); SP.current = null; setSheet(0.38, true); return; }
      setSheet(HEIGHTS.reduce((a, b) => (Math.abs(b - sheetH) < Math.abs(a - sheetH) ? b : a)), true);
      try { localStorage.setItem('loop3d.editor.sheet', String(sheetH)); } catch (er) { /* private mode */ }
      if (SP.current === 'logic') LE.logic.paint();
    };
  });

  // ---------------------------------------------------------------- sound: the engine mixes, the page plays
  let audioCtx = null;
  LE.soundOn = () => !!audioCtx;
  LE.toggleSound = () => {
    if (!E.M) return;
    if (audioCtx) { audioCtx.close(); audioCtx = null; return; }
    audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 48000 });
    const frames = LoopUI.setting(E.M, 'audio.buffer', 2048), node = audioCtx.createScriptProcessor(frames, 0, 2);
    const ptr = E.M._malloc(frames * 2 * 4);
    node.onaudioprocess = (e) => {
      E.M._loop_audio(ptr, frames);
      const buf = E.M.HEAPF32.subarray(ptr >> 2, (ptr >> 2) + frames * 2);
      const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
      for (let i = 0; i < frames; i++) { L[i] = buf[i * 2]; R[i] = buf[i * 2 + 1]; }
    };
    node.connect(audioCtx.destination);
  };

  // ---------------------------------------------------------------- the menu and the scene picker
  function scenePicker() {
    const list = h('div', { class: 'content' });
    for (const p of E.index.projects) {
      if (!p.scenes.length) continue;
      list.append(h('div', { class: 'mh', style: 'padding:10px 4px 4px;color:var(--faint);font-size:12px;font-weight:600;text-transform:uppercase;letter-spacing:.04em' }, p.title));
      for (const s of p.scenes) list.append(h('button', { class: 'item' + (s.name === E.scene ? ' on' : ''), onclick: () => { m.close(); openScene(s.name); } }, LE.icon(s.name === p.start ? 'home' : 'place'), h('span', { class: 't' }, s.title), h('span', { class: 'r' }, s.name)));
    }
    const m = LE.modal({ title: 'Open a scene', body: list });
  }
  function versions() {
    const list = h('div', { class: 'content' });
    if (!H.versions.length) list.append(h('div', { class: 'empty' }, 'No changes yet. Every change you make is kept here.'));
    const fmt = (t) => new Date(t).toLocaleString(undefined, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit' });
    H.versions.slice(0, 200).forEach((v, i) => list.append(h('button', { class: 'item', onclick: () => { m.close(); if (H.commit(v.text, 'went back to: ' + v.label)) SP.rerun(); } }, LE.icon(i ? 'history' : 'check'), h('span', { class: 't' }, v.label), h('span', { class: 'r' }, fmt(v.time)))));
    list.append(h('button', { class: 'item', onclick: () => { m.close(); if (H.revert()) SP.rerun(); } }, LE.icon('back'), h('span', { class: 't' }, 'The file as it is on the site'), h('span', { class: 'r' }, '')));
    const m = LE.modal({ title: 'Versions of ' + (E.scene || 'this scene'), body: list });
  }
  function help() {
    LE.info('How the editor works', [
      '<p><b>The world fills the screen.</b> Tap a thing to pick it; drag a picked thing to slide it on the floor. Its handles lift it (↕), turn it (⟲) and size it (⤢). Double tap frames it.</p>',
      '<p><b>Moving the view:</b> one finger on empty space turns round what you look at; two fingers slide, pinch and twist. On a computer: right drag turns, shift drag slides, the wheel zooms, W A S D Q E fly, F frames.</p>',
      '<p><b>Four spaces</b> at the bottom: Build (things), Logic (the script), Film (shots), Sound. They change the tools shown, never what a gesture does.</p>',
      '<p><b>Nothing is lost.</b> Every change is kept in this browser the moment you make it, undo survives a reload, and Menu → Versions goes back to any earlier one.</p>',
      '<p><b>The script is the world.</b> Moving a thing rewrites its line; Logic shows the line. Copy it there and paste it in the chat to keep it in the repo.</p>',
      '<p><b>The Studio:</b> pick a thing and tap Open. On its Move bench, slide the white line to a moment and move the thing: that keeps a key. A key pins a value to a moment; the engine fills in every moment between. Done goes back to the world.</p>',
      '<p><b>Play</b> walks you through the world as it runs. Your changes stay.</p>',
      '<p>Keys: Ctrl+Z undo, Ctrl+Shift+Z redo, Delete removes, Ctrl+D copies, Esc lets go, P plays, 1 to 4 pick a space, Ctrl+K finds anything.</p>'].join(''));
  }
  async function screenshot() {
    const shot = await E.capture();
    const c = document.createElement('canvas');
    c.width = shot.w; c.height = shot.h;
    c.getContext('2d').putImageData(new ImageData(shot.px, shot.w, shot.h), 0, 0);
    c.toBlob((b) => LE.download((E.scene || 'loop3d') + '.png', b), 'image/png');
  }
  const cmds = [
    ['openScene', 'Open a scene…', scenePicker],
    ['versions', 'Versions (every change)', versions],
    ['copyScript', 'Copy the script', () => LE.logic.copy()],
    ['saveScript', 'Save the .loop file', () => LE.download((E.scene || 'scene') + '.loop', H.text)],
    ['player', 'Play it in the player', () => { LoopUI.handTo(E.scene, H.text); open('index.html?from=editor#' + E.scene, '_blank'); }],
    ['screenshot', 'Save a picture of the view', screenshot],
    ['benchmark', 'Measure how smoothly it runs', () => open('index.html?bench=scene#' + E.scene, '_blank')],
    ['projectSettings', 'Project settings', (id) => LE.settingsWindow('project', id)],
    ['editorPrefs', 'Editor preferences', (id) => LE.settingsWindow('editor', id)],
    ['revert', 'Back to the file', () => { if (H.revert()) SP.rerun(); }],
    ['help', 'How the editor works', help],
  ];
  for (const [id, label, run] of cmds) LE.cmd(id, label, run);
  LE.cmd('quality0', 'Quality: Phone', () => E.setQuality(0));
  LE.cmd('quality1', 'Quality: High', () => E.setQuality(1));
  LE.cmd('quality2', 'Quality: Ultra (path traced)', () => E.setQuality(2));
  $('#menuBtn').onclick = (e) => {
    const r = e.currentTarget.getBoundingClientRect();
    LE.showMenu([
      { label: 'Open a scene…', cmd: 'openScene' },
      { label: 'Find anything…', run: () => LE.palette(), key: 'Ctrl+K' },
      '-',
      { label: 'Versions (every change)', cmd: 'versions' },
      { label: 'Copy the script', cmd: 'copyScript' },
      { label: 'Save the .loop file', cmd: 'saveScript' },
      { label: 'Back to the file', cmd: 'revert', disabled: () => !H.edited() },
      '-',
      { header: 'Quality' },
      ...[0, 1, 2].map((q) => ({ label: QN[q] + (q === 2 ? ' (path traced)' : ''), run: () => E.setQuality(q), check: () => E.quality === q })),
      '-',
      { label: 'Play it in the player', cmd: 'player' },
      { label: 'Save a picture of the view', cmd: 'screenshot' },
      { label: 'Measure how smoothly it runs', cmd: 'benchmark' },
      '-',
      { label: 'Project settings', cmd: 'projectSettings' },
      { label: 'Editor preferences', cmd: 'editorPrefs' },
      { label: 'How the editor works', cmd: 'help' },
    ], r.left, r.bottom + 4, e.currentTarget);
  };
  $('#sceneBtn').onclick = scenePicker;

  // ---------------------------------------------------------------- keys
  const typing = (el) => el && (el.tagName === 'TEXTAREA' || el.tagName === 'INPUT' || el.tagName === 'SELECT' || el.isContentEditable);
  addEventListener('keydown', (e) => {
    const mod = e.ctrlKey || e.metaKey;
    if (mod && (e.key === 'k' || e.key === 'p')) { e.preventDefault(); LE.palette(); return; }
    if (typing(e.target) || document.querySelector('.modal-back')) return;
    if (V.mode === 'play') {
      const km = LoopUI.keyMap(E.M);
      if (e.code in km) { E.key(km[e.code], true); e.preventDefault(); }
      if (e.code === LoopUI.useKey(E.M) && !e.repeat) E.useKey();
      if (e.key === 'Escape' || e.key === 'p') V.setMode('edit');
      return;
    }
    if (mod && e.key.toLowerCase() === 'z') { e.preventDefault(); step(e.shiftKey ? 'redo' : 'undo'); return; }
    if (mod && e.key.toLowerCase() === 'y') { e.preventDefault(); step('redo'); return; }
    if (LE.studio.open) {
      // in the Studio: Esc is Done, Space plays, Delete removes the picked key (never the thing)
      if (mod) return;
      if (e.key === 'Escape') LE.studio.leave();
      else if (e.key === ' ') { e.preventDefault(); LE.studio.togglePlay(); }
      else if (e.key === 'Delete' || e.key === 'Backspace') { e.preventDefault(); LE.studio.removeKey(); }
      else if (e.key === 'k' || e.key === 'K') LE.studio.keyHere();
      else if (e.key === 'f' || e.key === 'F') V.frame();
      else if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'ShiftLeft', 'ShiftRight'].includes(e.code)) V.keyDown(e.code);
      return;
    }
    if (mod && e.key.toLowerCase() === 'd') { e.preventDefault(); if (V.sel) dupe(); return; }
    if (mod && e.key.toLowerCase() === 's') { e.preventDefault(); LE.toast('Every change is already kept. To keep it in the repo: Logic → Copy, then paste it in the chat.'); return; }
    if (mod) return;
    if (e.key === 'Delete' || e.key === 'Backspace') { if (V.sel || V.selMark) { e.preventDefault(); remove(); } return; }
    if (e.key === 'Escape') { V.select(null); V.selMark = null; SP.redraw(); return; }
    if (e.key === 'f' || e.key === 'F') { if (V.sel) V.frame(); return; }
    if (e.key === 'p' || e.key === 'P') { V.setMode('play'); return; }
    if (/^[1-4]$/.test(e.key)) { SP.show(['build', 'logic', 'film', 'sound'][+e.key - 1]); return; }
    if (['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'ShiftLeft', 'ShiftRight'].includes(e.code)) V.keyDown(e.code);
  });
  addEventListener('keyup', (e) => {
    V.keyUp(e.code);
    if (V.mode === 'play' && E.M) { const km = LoopUI.keyMap(E.M); if (e.code in km) E.key(km[e.code], false); }
  });
  addEventListener('blur', () => { ['KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'ShiftLeft', 'ShiftRight'].forEach((k) => V.keyUp(k)); });
  // Build's copy and remove, from the keys
  function dupe() { const b = SP.buildApi; if (b) b.duplicate(); }
  function remove() { const b = SP.buildApi; if (b) b.removeAny(); }
  LoopUI.gamepad(() => E.M, () => E.useKey(), () => {});

  // ---------------------------------------------------------------- speed
  setInterval(() => {
    if (!E.M || !E.ready) return;
    const s = E.stats();
    $('#speed').textContent = Math.round(s.fps || 0) + ' fps · ' + (s.gpu || 0).toFixed(1) + ' ms · ' + QN[E.quality];
  }, 500);

  E.boot();  // async: picks one core or many first
})();
