// What the player page and the editor share: dialogue lines and choices on
// screen, the fade between scenes, keys, the gamepad and the thumb sticks,
// all following the engine settings (story.*, keys.*, player.*), so a
// project, a scene's script or the editor can change them.
window.LoopUI = (() => {
  // a setting's value in effect (engine/core/settings.h), or the fallback
  function setting(M, id, fallback) {
    if (!M || typeof M._loop_get_setting !== 'function') return fallback;
    const n = M.lengthBytesUTF8(id) + 1, p = M._malloc(n);
    M.stringToUTF8(id, p, n);
    try {
      const v = JSON.parse(M.UTF8ToString(M._loop_get_setting(p)));
      return v === null || v === undefined ? fallback : v;
    } catch (e) {
      return fallback;
    } finally {
      M._free(p);
    }
  }

  // a line of dialogue: fades in, stays long enough to read, fades out
  function showLine(box, M, m) {
    const d = document.createElement('div');
    d.className = 'line';
    d.style.fontSize = (14 * setting(M, 'story.subtitleSize', 1)) + 'px';
    if (m.speaker && setting(M, 'story.speakerNames', true)) {
      const w = document.createElement('span');
      w.className = 'who';
      w.style.color = setting(M, 'story.speakerColor', '#ffd27a');
      w.textContent = m.speaker;
      d.appendChild(w);
    }
    d.appendChild(document.createTextNode(m.text));
    box.appendChild(d);
    while (box.children.length > 2) box.firstChild.remove();
    setTimeout(() => { d.style.opacity = '0'; setTimeout(() => d.remove(), 700); }, m.seconds * 1000);
  }

  // a question with buttons; story.choiceTimeout picks for you when time runs out
  let askTimer = 0;
  function showAsk(box, M, m, pick) {
    clearTimeout(askTimer);
    box.innerHTML = '';
    if (m.text) { const q = document.createElement('div'); q.className = 'q'; q.textContent = m.text; box.appendChild(q); }
    const choose = (i) => { clearTimeout(askTimer); box.style.display = 'none'; pick(i); };
    m.options.forEach((o, i) => {
      const b = document.createElement('button');
      b.textContent = o;
      b.onclick = () => choose(i);
      box.appendChild(b);
    });
    box.style.display = 'flex';
    const secs = setting(M, 'story.choiceTimeout', 0);
    if (secs > 0) {
      const which = setting(M, 'story.timeoutPick', 0);
      const bar = document.createElement('div');
      bar.className = 'timeout';
      bar.style.cssText = 'height:3px;background:currentColor;opacity:.5;transition:width ' + secs + 's linear;width:100%';
      box.appendChild(bar);
      requestAnimationFrame(() => { bar.style.width = '0%'; });
      askTimer = setTimeout(() => choose(which < 0 ? Math.floor(Math.random() * m.options.length) : Math.min(which, m.options.length - 1)), secs * 1000);
    }
  }

  // the text shown while a scene loads (story.loadingText, {scene} = its title)
  const loadingText = (M, title) => String(setting(M, 'story.loadingText', 'Loading {scene}…')).replace('{scene}', title || 'the next scene');

  // a black veil for the fade between scenes (story.fade seconds)
  function fader(parent) {
    let v = parent.querySelector('.loop-fade');
    if (!v) {
      v = document.createElement('div');
      v.className = 'loop-fade';
      v.style.cssText = 'position:absolute;inset:0;background:#000;opacity:0;pointer-events:none;transition:opacity .5s';
      parent.appendChild(v);
    }
    return {
      out(M) { const s = setting(M, 'story.fade', 0.6); v.style.transitionDuration = s + 's'; v.style.opacity = s > 0 ? '1' : '0'; return new Promise((r) => setTimeout(r, s * 1000)); },
      in(M) { v.style.transitionDuration = setting(M, 'story.fade', 0.6) + 's'; v.style.opacity = '0'; },
    };
  }

  // keys.* → the engine's walking keys (0 forward, 1 back, 2 left, 3 right, 4 run)
  function keyMap(M) {
    const m = { ArrowUp: 0, ArrowDown: 1, ArrowLeft: 2, ArrowRight: 3, ShiftRight: 4 };
    ['keys.forward', 'keys.back', 'keys.left', 'keys.right', 'keys.run'].forEach((id, i) => { m[setting(M, id, ['KeyW', 'KeyS', 'KeyA', 'KeyD', 'ShiftLeft'][i])] = i; });
    return m;
  }
  const useKey = (M) => setting(M, 'keys.use', 'KeyE');

  // a gamepad: left stick walks, right stick looks, A uses, a shoulder runs
  function gamepad(getM, onUse, onActive) {
    let wasA = false, moving = false;
    function poll() {
      const M = getM();
      const pads = navigator.getGamepads ? navigator.getGamepads() : [];
      const p = pads && [...pads].find((x) => x);
      if (M && p) {
        const dz = setting(M, 'player.deadzone', 0.15);
        const ax = (v) => (Math.abs(v) < dz ? 0 : (v - Math.sign(v) * dz) / (1 - dz));
        const lx = ax(p.axes[0] || 0), ly = ax(p.axes[1] || 0), rx = ax(p.axes[2] || 0), ry = ax(p.axes[3] || 0);
        if (lx || ly || moving) { M._loop_move(lx, -ly); moving = !!(lx || ly); if (moving && onActive) onActive(); }
        if (rx || ry) M._loop_look(rx * 14, ry * 14);
        M._loop_key(4, p.buttons[4]?.pressed || p.buttons[5]?.pressed || p.buttons[10]?.pressed ? 1 : 0);
        const a = !!p.buttons[0]?.pressed;
        if (a && !wasA) onUse();
        wasA = a;
      }
      requestAnimationFrame(poll);
    }
    addEventListener('gamepadconnected', () => {});
    requestAnimationFrame(poll);
  }

  // story.hud and player.stickSize, and pausing while the page is hidden
  function applyPage(M, opts = {}) {
    const size = setting(M, 'player.stickSize', 110);
    document.querySelectorAll('.stick').forEach((s) => { s.style.width = s.style.height = size + 'px'; });
    if (opts.hud) opts.hud.forEach((el) => { el.style.display = setting(M, 'story.hud', true) ? '' : 'none'; });
  }
  function pauseWhenHidden(getM) {
    let pausedByUs = false;
    document.addEventListener('visibilitychange', () => {
      const M = getM();
      if (!M || !M._loop_set_option) return;
      if (document.hidden && setting(M, 'render.pauseHidden', true)) {
        const n = M.lengthBytesUTF8('pause') + 1, p = M._malloc(n);
        M.stringToUTF8('pause', p, n); M._loop_set_option(p, 1); M._free(p);
        pausedByUs = true;
      } else if (!document.hidden && pausedByUs) {
        const n = M.lengthBytesUTF8('pause') + 1, p = M._malloc(n);
        M.stringToUTF8('pause', p, n); M._loop_set_option(p, 0); M._free(p);
        pausedByUs = false;
      }
    });
  }

  // `use "name"` brings in a script from the same folder (once). Shared
  // scripts go first and the scene's own lines keep their numbers after them.
  async function assemble(text, fetchScene) {
    const seen = new Set(), pre = [], own = [];
    async function expand(t) {
      const out = [];
      for (const line of t.split('\n')) {
        const m = line.match(/^\s*use\s+"([^"]+)"/);
        if (!m) { out.push(line); continue; }
        if (!seen.has(m[1])) { seen.add(m[1]); try { out.push(await expand(await fetchScene(m[1]))); } catch (e) { out.push('say "can\'t find ' + m[1] + '.loop"'); } }
        out.push('');
      }
      return out.join('\n');
    }
    for (const line of text.split('\n')) {
      const m = line.match(/^\s*use\s+"([^"]+)"/);
      if (m && !seen.has(m[1])) {
        seen.add(m[1]);
        try { pre.push(await expand(await fetchScene(m[1]))); } catch (e) { pre.push('say "can\'t find ' + m[1] + '.loop"'); }
        own.push('');
      } else own.push(m ? '' : line);
    }
    const head = pre.length ? pre.join('\n') + '\n' : '';
    return { text: head + own.join('\n'), offset: head ? head.split('\n').length - 1 : 0 };
  }

  // Models (`model "x.glb"`) are files the engine reads: fetch each one the
  // script names from scenes/models/ into the engine's own folder /scene/models
  // before the script runs (each file once per page).
  async function loadModels(M, text, onProgress) {
    if (!M || !M.FS) return;
    const names = new Set();
    for (const m of text.matchAll(/\bmodel\s*\(?\s*(?:file:\s*)?"([^"]+\.glb)"/g)) names.add(m[1]);
    try { M.FS.mkdirTree('/scene/models'); } catch (e) {}
    if (names.size) names.add('materials.json');  // the models' real values, if the site has them
    let done = 0;
    await Promise.all([...names].map(async (name) => {
      const path = '/scene/models/' + name;
      try { M.FS.stat(path); done++; return; } catch (e) {}
      try {
        const r = await fetch('scenes/models/' + encodeURIComponent(name));
        if (!r.ok) return;  // the script then says which model is missing
        M.FS.writeFile(path, new Uint8Array(await r.arrayBuffer()));
      } catch (e) {}
      done++;
      if (onProgress) onProgress(done, names.size);
    }));
  }

  // What the editor hands to the player: the scene it has open, edits and all
  // (kept in this browser), so "Play" plays exactly what's in the editor.
  const HANDOFF = 'loop3d.play';
  function handTo(scene, text, project) {
    try { localStorage.setItem(HANDOFF, JSON.stringify({ scene, text, project, at: Date.now() })); return true; } catch (e) { return false; }
  }
  function handed() {
    try { return JSON.parse(localStorage.getItem(HANDOFF) || 'null'); } catch (e) { return null; }
  }

  // A recording's headline numbers, from its scenes (or parts of scenes).
  function summary(segs) {
    const frames = segs.reduce((t, x) => t + x.frames, 0), secs = segs.reduce((t, x) => t + x.seconds, 0);
    const worst = (k, f) => (segs.length ? f(...segs.map((x) => x[k])) : 0);
    return {
      scenes: new Set(segs.map((x) => x.scene)).size, parts: segs.length, seconds: +secs.toFixed(1), frames,
      fps: +(secs ? frames / secs : 0).toFixed(2), worstLow1Fps: worst('low1Fps', Math.min), worstP99Ms: worst('p99Ms', Math.max),
      maxMs: worst('maxMs', Math.max), hitches: segs.reduce((t, x) => t + x.hitches, 0),
      avgScale: +(segs.reduce((t, x) => t + x.scaleAvg * x.seconds, 0) / (secs || 1)).toFixed(3), worstLoadMs: worst('loadMs', Math.max),
    };
  }

  // The live recorder: whatever is played, as it is played, timed scene by
  // scene in parts of 30 s (a part is also cut when the quality changes or the
  // page goes to the background), each part saved as it ends, so a visit
  // closed halfway still keeps what came before. The engine times the frames
  // (loop_perf_*); this decides when a part starts and ends.
  //   o.getM()      the engine (or null while it loads)
  //   o.quality()   'Phone', 'High' or 'Ultra' now
  //   o.store       where the visit is saved: { save(visit) }
  //   o.build, o.device (a promise), o.partMs, o.clock (tests)
  function recorder(o) {
    const clock = o.clock || (() => new Date()), partMs = o.partMs || 30000;
    const t0 = clock(), stamp = t0.toISOString().slice(0, 19).replace(/[-:]/g, '').replace('T', '-');
    const visit = { kind: 'loop3d-bench', v: 1, when: t0.toISOString(), build: o.build || '', bench: 'live', start: null, quality: o.quality(),
                    device: null, summary: summary([]), segments: [] };
    const id = 'live-' + stamp + '-' + Math.random().toString(36).slice(2, 6);
    let scene = null, part = 0, quality = o.quality(), on = false;
    if (o.device) o.device.then((d) => { visit.device = d; }).catch(() => {});
    const engine = () => { const M = o.getM(); return M && M._loop_perf_part ? M : null; };
    // the numbers since the last cut, as the next part of this scene
    function cut() {
      const M = engine();
      if (!M || !on || !scene) return false;
      let s = null;
      try { s = JSON.parse(M.UTF8ToString(M._loop_perf_part())); } catch (e) { return false; }
      const label = quality;
      quality = o.quality();
      if (!s.frames) return false;
      visit.segments.push({ ...s, scene, part: part++, quality: label, at: clock().toISOString() });
      visit.summary = summary(visit.segments);
      o.store.save(visit, id);
      return true;
    }
    const timer = setInterval(cut, partMs);
    const r = {
      id, visit, cut,
      // a scene starts loading: the last one's part ends, the new one's
      // recording starts with its first frame (so its load time counts)
      scene(name) {
        const M = engine();
        if (!M) return;
        cut();
        if (!visit.start) visit.start = name;
        scene = name, part = 0, quality = o.quality(), on = true;
        M._loop_perf_begin();
      },
      // before the quality changes: the part so far keeps the old one's name
      quality() { cut(); },
      // the page goes to the background, and comes back
      hidden() { const M = engine(); if (!M || !on) return; cut(); M._loop_perf_end(); on = false; },
      shown() { const M = engine(); if (!M || on || !scene) return; M._loop_perf_go(); quality = o.quality(); on = true; },
      stop() { clearInterval(timer); r.hidden(); },
    };
    return r;
  }

  // Where a live recording goes: this browser always (the menu's "Copy my
  // recording" copies it); and, on the recording page (the player inside a
  // claude.ai page that keeps a database: window.claude), one row per visit,
  // runs/<visit id>, which Claude reads. Writes go one at a time, only when a
  // part was added. A row holds at most 256 KB, so a visit fills rows of 150
  // parts (75 minutes): <id>, then <id>-2, <id>-3 …, each a run of its own.
  const PARTS_PER_ROW = 150;
  function recordingStore(win, onSaved) {
    win = win || window;
    let db = null, asked = false, writing = false, pending = null, failed = false, again = false;
    function ask() {
      if (asked || !win.claude || !win.claude.use) return;
      asked = true;
      win.claude.use('db').then((d) => { db = d; if (pending) flush(); }).catch(() => {});
    }
    async function flush() {
      if (!db || writing || !pending || failed) return;
      writing = true;
      const { visit, id } = pending;
      pending = null;
      const k = Math.max(0, Math.ceil(visit.segments.length / PARTS_PER_ROW) - 1), rowId = k ? id + '-' + (k + 1) : id;
      const segs = visit.segments.slice(k * PARTS_PER_ROW);
      const body = JSON.parse(JSON.stringify({ ...visit, id: rowId, start: segs.length ? segs[0].scene : visit.start, summary: summary(segs), segments: segs }));
      let retry = false;
      try {
        await db.doc('runs/' + rowId).set(body);
        if (onSaved) onSaved(visit, true);
      } catch (e) {
        // a passing hiccup: once more shortly (the next part writes it all again anyway)
        if (e && e.code === 'unavailable' && !again) { again = true; retry = !pending; if (retry) pending = { visit, id }; }
        else if (!(e && e.code === 'unavailable')) { failed = true; if (onSaved) onSaved(visit, false, e && e.code); }
      }
      if (!retry) again = false;
      writing = false;
      if (pending) setTimeout(flush, retry ? 1000 + Math.random() * 1000 : 0);
    }
    ask();
    return {
      save(visit, id) {
        try { win.localStorage.setItem('loop3d.live', JSON.stringify(visit)); } catch (e) {}
        ask();
        pending = { visit, id };
        flush();
      },
      get toClaude() { return !!db; },
    };
  }

  const api = { setting, showLine, showAsk, loadingText, fader, keyMap, useKey, gamepad, applyPage, pauseWhenHidden, assemble, loadModels, handTo, handed,
                summary, recorder, recordingStore };
  if (typeof module !== 'undefined') module.exports = api;
  return api;
})();
