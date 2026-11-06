// The world view: the editor's camera, picking, the selection and its
// handles, and moving things, which rewrites their line in the script.
//
//   one finger on a thing: picks it; drag a picked thing: slides it on the floor
//   one finger on empty space: turns the view round what you look at
//   two fingers: slide and pinch the view (always, whatever is picked)
//   the handles: ↕ lifts, ⟲ turns, ⤢ sizes; double tap frames a thing
//   computer: left drag picks and moves, right or middle drag turns, shift slides,
//   the wheel zooms, W A S D Q E fly, F frames, Delete removes, Ctrl+D copies
window.LE = window.LE || {};
(() => {
  const E = LE.engine, S = LE.source, H = LE.history;
  const $ = (s) => document.querySelector(s);
  const canvas = $('#view'), ov = $('#overlay'), layer = $('#touch'), hintEl = $('#hint');
  const ctx = ov.getContext('2d');
  const D2R = Math.PI / 180;
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
  const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const len = (a) => Math.hypot(a[0], a[1], a[2]);
  const norm = (a) => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  const snapTo = (v, s) => (s > 0 ? Math.round(v / s) * s : v);

  const V = {
    mode: 'edit',            // edit (the editor's camera), play (walk), film (the shots)
    cam: { target: [0, 1, 0], dist: 8, yaw: 0, pitch: 0.4, lens: 24 },
    sel: null,               // {index, line (in the scene's own text), info, word, many, group}
    snap: true, moveStep: 0.05, turnStep: 15 * D2R,
    workLight: false,        // a lamp at the editor camera, to see while building in the dark (never part of the world)
    extra: [],               // marks: lamps and sounds, from their lines: {at, glyph, label, color, line, movable}
    selMark: null,           // the mark picked
    listeners: [],
    on(fn) { V.listeners.push(fn); },
    emit(kind, data) { V.listeners.forEach((fn) => { try { fn(kind, data); } catch (e) { console.error(e); } }); },
  };
  LE.view = V;

  // ---------------------------------------------------------------- the editor camera
  function camAt() {
    const c = V.cam, cp = Math.cos(c.pitch);
    return [c.target[0] + Math.sin(c.yaw) * cp * c.dist, c.target[1] + Math.sin(c.pitch) * c.dist, c.target[2] + Math.cos(c.yaw) * cp * c.dist];
  }
  V.pushCamera = () => { if (E.M) E.editCamera({ on: V.mode === 'edit', at: camAt(), look_at: V.cam.target, lens: V.cam.lens }); };
  // the editor camera takes over from wherever the view is (the scene's camera, the walk)
  V.cameraFrom = (at, look) => {
    // a walking view looks one metre ahead: turn round a point a few metres off instead
    if (len(sub(look, at)) < 1.5) look = add(at, mul(norm(sub(look, at)), 4));
    const d = sub(at, look), dist = Math.max(0.5, len(d));
    V.cam.target = look.slice();
    V.cam.dist = dist;
    V.cam.pitch = Math.max(-1.45, Math.min(1.45, Math.asin(d[1] / dist)));
    V.cam.yaw = Math.atan2(d[0], d[2]);
    V.pushCamera();
  };
  V.setWorkLight = (on) => { V.workLight = !!on; E.setOption('worklight', V.workLight ? 1 : 0); V.emit('worklight', V.workLight); };
  V.setMode = (m) => {
    if (m === V.mode) return;
    const v = E.view();
    V.mode = m;
    if (m === 'edit') {
      E.setMode(false);
      if (v && !v.edit) V.cameraFrom(v.at, v.look_at); else V.pushCamera();
    } else {
      E.editCamera({ on: false });
      E.setMode(m === 'film');
    }
    document.body.classList.toggle('playing', m === 'play');
    V.emit('mode', m);
  };
  V.frame = (index) => {
    const t = E.thing(index ?? (V.sel && V.sel.index));
    if (!t) return;
    const b = worldBox(t), c = mul(add(b.lo, b.hi), 0.5), size = len(sub(b.hi, b.lo));
    V.cam.target = c;
    V.cam.dist = Math.max(1.2, size * 1.6 + 0.8);
    V.pushCamera();
  };

  // ---------------------------------------------------------------- rays and screen points
  function viewRay(px, py) {
    const r = canvas.getBoundingClientRect(), v = E.view();
    if (!v) return null;
    const x = (px - r.left) / r.width, y = (py - r.top) / r.height;
    const ty = Math.tan(v.fovY / 2), tx = ty * v.aspect;
    const f = norm(sub(v.look_at, v.at)), rt = norm(cross(f, [0, 1, 0])), up = cross(rt, f);
    return { o: v.at, d: norm(add(f, add(mul(rt, (2 * x - 1) * tx), mul(up, (1 - 2 * y) * ty)))), x, y, v };
  }
  const onPlane = (ray, h) => (Math.abs(ray.d[1]) < 1e-6 ? null : ((t) => (t > 0 ? add(ray.o, mul(ray.d, t)) : null))((h - ray.o[1]) / ray.d[1]));
  let VP = null, W = 0, Hh = 0;
  function project(p) {
    if (!VP) return null;
    const x = VP[0] * p[0] + VP[4] * p[1] + VP[8] * p[2] + VP[12];
    const y = VP[1] * p[0] + VP[5] * p[1] + VP[9] * p[2] + VP[13];
    const w = VP[3] * p[0] + VP[7] * p[1] + VP[11] * p[2] + VP[15];
    if (w <= 1e-4) return null;
    return [(x / w * 0.5 + 0.5) * W, (1 - (y / w * 0.5 + 0.5)) * Hh, w];
  }
  const xform = (m, p) => [m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]];
  function corners(t) {
    const out = [];
    for (let i = 0; i < 8; i++) out.push(xform(t.matrix, [i & 1 ? t.hi[0] : t.lo[0], i & 2 ? t.hi[1] : t.lo[1], i & 4 ? t.hi[2] : t.lo[2]]));
    return out;
  }
  function worldBox(t) {
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const p of corners(t)) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], p[k]); hi[k] = Math.max(hi[k], p[k]); }
    return { lo, hi };
  }

  // ---------------------------------------------------------------- the selection
  // What a thing is called: its name, else its word and material ("box · walnut").
  V.describe = (t, lineText) => {
    if (!t) return '';
    if (t.name) return t.name;
    const call = lineText ? S.callOf(lineText) : null;
    const word = call ? (call.name || call.word) : ({ b: 'box', s: 'ball', c: 'capsule' }[t.kind] || 'thing');
    return word + (t.material && !/^colour/.test(t.material) ? ' · ' + t.material : '');
  };
  function sceneLine(engineLine) { return engineLine - (E.lineOffset || 0); }
  V.select = (index, quiet) => {
    if (index == null || index < 0) { V.sel = null; V.emit('select', null); hint(''); return; }
    const t = E.thing(index);
    if (!t) { V.sel = null; V.emit('select', null); return; }
    const line = t.line > 0 ? sceneLine(t.line) : 0;
    const lineText = line > 0 ? S.lineAt(H.text, line) : '';
    V.sel = { index, line, info: t, lineText, many: t.lineCount > 1, name: V.describe(t, lineText) };
    V.emit('select', V.sel);
    if (!quiet) {
      const sz = sub(worldBox(t).hi, worldBox(t).lo).map((x) => S.num(x)).join(' × ');
      hint(V.sel.name + ' · ' + sz + ' m' + (line > 0 ? ' · line ' + line : t.line > 0 ? ' · from a shared script' : ' · made by the engine') + (V.sel.many ? ' · one of ' + t.lineCount + ' from a repeat' : ''));
    }
  };
  // after the script runs again, the same line's thing is picked again
  V.reselect = () => {
    if (!V.sel || V.sel.line <= 0) return V.select(null);
    const info = E.sceneInfo();
    if (!info || !info.things) return;
    const want = V.sel.line + (E.lineOffset || 0);
    const i = info.things.findIndex((t) => t.line === want);
    V.select(i >= 0 ? i : null, true);
  };
  V.selectLine = (line) => {
    const info = E.sceneInfo();
    if (!info || !info.things) return false;
    const want = line + (E.lineOffset || 0);
    const i = info.things.findIndex((t) => t.line === want);
    if (i >= 0) { V.select(i); return true; }
    return false;
  };
  let hintT = 0;
  function hint(text, ms) {
    clearTimeout(hintT);
    hintEl.textContent = text;
    hintEl.classList.toggle('on', !!text);
    if (text && ms) hintT = setTimeout(() => hintEl.classList.remove('on'), ms);
  }
  V.hint = hint;

  // Can this thing be changed from the editor? Says why not.
  function editable(sel) {
    if (!sel) return false;
    if (sel.line > 0) return true;
    hint(sel.info.line > 0 ? 'This one is made in a shared script (use "…"): open that script to move it.' : 'The engine made this one (part of a person or a creature): it moves by itself.', 3200);
    return false;
  }
  // All the things one line makes (a repeat): their indices and where they are.
  function groupOf(sel) {
    if (!sel.many) return [{ index: sel.index, at: sel.info.position.slice() }];
    const info = E.sceneInfo();
    const want = sel.info.line;
    return info.things.map((t, i) => ({ t, i })).filter((x) => x.t.line === want).map((x) => ({ index: x.i, at: x.t.position.slice() }));
  }

  // A change to the selected thing, written into its line: at, turn or scale.
  function writeBack(kind, sel, before, after, group) {
    const lineNo = sel.line;
    const old = S.lineAt(H.text, lineNo);
    let line = old;
    if (kind === 'move') line = S.moveLine(old, after.at, sub(after.at, before.at), sel.many);
    else if (kind === 'turn') line = S.turnLine(old, after.rotation[0], after.rotation[0] - before.rotation[0], sel.many);
    else if (kind === 'size') line = sel.many ? old : S.scaleLine(old, after.scale);
    if (line === old) return;
    // a repeat's turn goes back by running the script again (each of its things had its own turn)
    const fast = kind === 'turn' && sel.many ? null : (group || [{ index: sel.index }]).map((g) => ({ index: g.index, before: g.before || before, after: g.after || after }));
    H.commit(S.replaceLine(H.text, lineNo, line), { move: 'moved ', turn: 'turned ', size: 'sized ' }[kind] + sel.name, fast);
    sel.lineText = line;
    V.emit('changed', { kind, line: lineNo });
  }

  // ---------------------------------------------------------------- gestures
  const pointers = new Map();
  let g = null;          // the gesture in progress
  let lastTap = { t: 0, x: 0, y: 0, index: -1 };
  const HANDLE_R = 22;
  let handles = [];      // [{kind, x, y}] where the handles were drawn

  function handleAt(x, y) {
    const r = canvas.getBoundingClientRect();
    for (const h of handles) if (Math.hypot(x - r.left - h.x, y - r.top - h.y) < HANDLE_R + 8) return h;
    return null;
  }
  layer.addEventListener('contextmenu', (e) => e.preventDefault());
  layer.addEventListener('pointerdown', (e) => {
    if (!E.M || V.mode === 'play') return;
    layer.setPointerCapture(e.pointerId);
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    canvas.focus({ preventScroll: true });
    if (V.mode === 'film') { V.setMode('edit'); V.emit('filmLeft'); }
    if (pointers.size === 2) { endGesture(true); startTwo(); return; }
    if (pointers.size > 2) return;
    const mouseTurn = e.pointerType === 'mouse' && (e.button === 1 || e.button === 2);
    if (mouseTurn) { g = { kind: e.shiftKey || e.button === 1 && e.shiftKey ? 'pan' : 'orbit', x: e.clientX, y: e.clientY, moved: true }; return; }
    if (e.pointerType === 'mouse' && e.shiftKey) { g = { kind: 'pan', x: e.clientX, y: e.clientY, moved: true }; return; }
    const h = V.sel && handleAt(e.clientX, e.clientY);
    if (h && editable(V.sel)) { startHandle(h, e); return; }
    const mk = markAt(e.clientX, e.clientY);
    if (mk) { pickMark(mk); if (mk.movable) startMarkMove(mk, e); return; }
    const ray = viewRay(e.clientX, e.clientY);
    const idx = ray ? E.pick(ray.x, ray.y) : -1;
    if (V.sel && idx === V.sel.index && V.sel.line > 0) { startMove(e, ray); return; }
    g = { kind: 'maybe', x: e.clientX, y: e.clientY, x0: e.clientX, y0: e.clientY, index: idx, moved: false };
  });
  layer.addEventListener('pointermove', (e) => {
    if (!pointers.has(e.pointerId)) return;
    const p = pointers.get(e.pointerId);
    p.x = e.clientX; p.y = e.clientY;
    if (!g) return;
    if (g.kind === 'two') return moveTwo();
    const dx = e.clientX - g.x, dy = e.clientY - g.y;
    if (g.kind === 'maybe') {
      if (Math.hypot(e.clientX - g.x0, e.clientY - g.y0) < 8) return;
      g.kind = 'orbit'; g.moved = true;
    }
    if (g.kind === 'orbit') {
      V.cam.yaw -= dx * 0.008;
      V.cam.pitch = Math.max(-1.45, Math.min(1.45, V.cam.pitch + dy * 0.006));
      g.x = e.clientX; g.y = e.clientY;
      V.pushCamera();
    } else if (g.kind === 'pan') {
      pan(dx, dy);
      g.x = e.clientX; g.y = e.clientY;
    } else if (g.kind === 'move') dragMove(e);
    else if (g.kind === 'lift' || g.kind === 'turn' || g.kind === 'size') dragHandle(e);
    else if (g.kind === 'mark') {
      const ray = viewRay(e.clientX, e.clientY), hit = ray && onPlane(ray, g.y);
      if (!hit) return;
      let at = add(hit, g.grab);
      if (V.snap) at = [snapTo(at[0], V.moveStep), at[1], snapTo(at[2], V.moveStep)];
      g.at = at; g.mark.at = at;
      hint(g.mark.label + ' · ' + S.num(at[0]) + ', ' + S.num(at[1]) + ', ' + S.num(at[2]) + ' m', 0);
    }
  });
  const up = (e) => {
    if (!pointers.has(e.pointerId)) return;
    pointers.delete(e.pointerId);
    if (g && g.kind === 'two') { if (pointers.size === 0) g = null; return; }
    if (pointers.size === 0) endGesture(false);
  };
  layer.addEventListener('pointerup', up);
  layer.addEventListener('pointercancel', up);
  layer.addEventListener('wheel', (e) => {
    e.preventDefault();
    if (V.mode !== 'edit') V.setMode('edit');
    if (e.ctrlKey) { V.cam.dist = Math.max(0.3, Math.min(5000, V.cam.dist * Math.exp(e.deltaY * 0.01))); }
    else V.cam.dist = Math.max(0.3, Math.min(5000, V.cam.dist * Math.exp(e.deltaY * 0.0015)));
    V.pushCamera();
  }, { passive: false });

  function pan(dx, dy) {
    const v = E.view();
    if (!v) return;
    const f = norm(sub(V.cam.target, camAt())), rt = norm(cross(f, [0, 1, 0])), upv = cross(rt, f);
    const k = 2 * Math.tan(v.fovY / 2) * V.cam.dist / Math.max(1, canvas.clientHeight);
    V.cam.target = add(V.cam.target, add(mul(rt, -dx * k), mul(upv, dy * k)));
    V.pushCamera();
  }
  function startTwo() {
    const [a, b] = [...pointers.values()];
    g = { kind: 'two', d: Math.hypot(a.x - b.x, a.y - b.y), mx: (a.x + b.x) / 2, my: (a.y + b.y) / 2, ang: Math.atan2(b.y - a.y, b.x - a.x), dist: V.cam.dist };
  }
  function moveTwo() {
    const [a, b] = [...pointers.values()];
    if (!a || !b) return;
    const d = Math.hypot(a.x - b.x, a.y - b.y), mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
    V.cam.dist = Math.max(0.3, Math.min(5000, g.dist * (g.d / Math.max(10, d))));
    // a twist of two fingers turns the view too
    const ang = Math.atan2(b.y - a.y, b.x - a.x);
    V.cam.yaw += (ang - g.ang);
    g.ang = ang;
    pan(mx - g.mx, my - g.my);
    g.mx = mx; g.my = my;
  }
  function endGesture(cancelled) {
    if (!g) return;
    const was = g;
    g = null;
    if (was.kind === 'maybe' && !cancelled) {
      const now = performance.now();
      if (was.index >= 0 && was.index === lastTap.index && now - lastTap.t < 350 && Math.hypot(was.x0 - lastTap.x, was.y0 - lastTap.y) < 30) {
        V.select(was.index); V.frame(was.index);
        lastTap = { t: 0, index: -1 };
        return;
      }
      lastTap = { t: now, x: was.x0, y: was.y0, index: was.index };
      if (V.selMark) { V.selMark = null; V.emit('mark', null); }
      V.select(was.index >= 0 ? was.index : null);
    } else if (was.kind === 'move' || was.kind === 'lift' || was.kind === 'turn' || was.kind === 'size') finishDrag(was);
    else if (was.kind === 'mark') finishMark(was);
  }

  // ---------------------------------------------------------------- marks: lamps and sounds
  function markAt(x, y) {
    const r = canvas.getBoundingClientRect();
    let best = null, bd = 26;
    for (const m of V.extra) if (m._p) { const d = Math.hypot(x - r.left - m._p[0], y - r.top - m._p[1]); if (d < bd) { bd = d; best = m; } }
    return best;
  }
  function pickMark(mk) {
    V.selMark = mk;
    if (V.sel) { V.sel = null; V.emit('select', null); }
    V.emit('mark', mk);
    hint(mk.label + ' · line ' + mk.line + (mk.movable ? ' · drag to move it' : mk.why ? ' · ' + mk.why : ''), 3000);
  }
  function startMarkMove(mk, e) {
    const ray = viewRay(e.clientX, e.clientY), hit = ray && onPlane(ray, mk.at[1]);
    if (!hit) return;
    g = { kind: 'mark', mark: mk, x0: e.clientX, y0: e.clientY, grab: sub(mk.at, hit), y: mk.at[1], before: mk.at.slice(), at: mk.at.slice() };
  }
  function finishMark(was) {
    const mk = was.mark, delta = sub(was.at, was.before);
    if (len(delta) < 1e-4) return;
    const old = S.lineAt(H.text, mk.line), line = S.moveLine(old, was.at, delta, false);
    if (line === old) return;
    H.commit(S.replaceLine(H.text, mk.line, line), 'moved ' + mk.label);
    V.emit('changed', { kind: 'mark', line: mk.line, rerun: true });
  }

  // slide on the floor: on the flat plane at the thing's height, keeping where it was grabbed
  function startMove(e, ray) {
    if (!editable(V.sel)) return;
    const t = E.thing(V.sel.index);
    const hit = onPlane(ray, t.position[1]);
    if (!hit) return;
    g = { kind: 'move', sel: V.sel, x: e.clientX, y: e.clientY, before: { at: t.position.slice() }, grab: sub(t.position, hit), y0: t.position[1], group: groupOf(V.sel), at: t.position.slice() };
    hint(V.sel.many ? 'Moving all ' + g.group.length + ' from this repeat' : 'Slide it where you want it', 0);
  }
  function dragMove(e) {
    const ray = viewRay(e.clientX, e.clientY);
    let hit = ray && onPlane(ray, g.y0);
    let at;
    // the floor seen edge-on (or the point far off): follow the finger on the
    // screen instead, at the thing's own distance, so it never flies away
    const far = hit && len(sub(hit, ray.o)) > Math.max(40, len(sub(g.before.at, ray.o)) * 3);
    if (!hit || far) {
      const v = ray ? ray.v : E.view();
      const f = norm(sub(v.look_at, v.at)), flat = norm([f[0], 0, f[2]]), rt = norm(cross(flat, [0, 1, 0]));
      const depth = Math.max(0.5, dot(sub(g.before.at, v.at), f));
      const k = 2 * Math.tan(v.fovY / 2) * depth / Math.max(1, canvas.clientHeight);
      at = add(g.before.at, add(mul(rt, (e.clientX - g.x) * k), mul(flat, -(e.clientY - g.y) * k)));
    } else at = add(hit, g.grab);
    if (V.snap) at = [snapTo(at[0], V.moveStep), at[1], snapTo(at[2], V.moveStep)];
    g.at = at;
    const delta = sub(at, g.before.at);
    for (const m of g.group) E.thingSet(m.index, { at: add(m.at, delta) });
    hint(S.num(at[0]) + ', ' + S.num(at[1]) + ', ' + S.num(at[2]) + ' m', 0);
  }
  function startHandle(h, e) {
    const t = E.thing(V.sel.index);
    const c = project(mul(add(worldBox(t).lo, worldBox(t).hi), 0.5));
    const r = canvas.getBoundingClientRect();
    g = { kind: h.kind, sel: V.sel, x0: e.clientX, y0: e.clientY, cx: c ? c[0] + r.left : e.clientX, cy: c ? c[1] + r.top : e.clientY,
      before: { at: t.position.slice(), rotation: t.rotation.slice(), scale: t.scale.slice() }, at: t.position.slice(), rotation: t.rotation.slice(), scale: t.scale.slice(),
      group: groupOf(V.sel), depth: c ? c[2] : V.cam.dist };
    g.a0 = Math.atan2(e.clientY - g.cy, e.clientX - g.cx);
    g.r0 = Math.max(12, Math.hypot(e.clientX - g.cx, e.clientY - g.cy));
    if (h.kind === 'size' && V.sel.many) { hint('A repeat\'s size is in its line: change it in Logic', 2600); g = null; }
  }
  function dragHandle(e) {
    const v = E.view();
    if (g.kind === 'lift') {
      const k = 2 * Math.tan(v.fovY / 2) * g.depth / Math.max(1, canvas.clientHeight);
      let y = g.before.at[1] - (e.clientY - g.y0) * k;
      if (V.snap) y = snapTo(y, V.moveStep);
      g.at = [g.before.at[0], y, g.before.at[2]];
      const delta = sub(g.at, g.before.at);
      for (const m of g.group) E.thingSet(m.index, { at: add(m.at, delta) });
      hint('height ' + S.num(y) + ' m', 0);
    } else if (g.kind === 'turn') {
      const a = Math.atan2(e.clientY - g.cy, e.clientX - g.cx);
      let d = -(a - g.a0);
      while (d > Math.PI) d -= 2 * Math.PI;
      while (d < -Math.PI) d += 2 * Math.PI;
      let yaw = g.before.rotation[0] + d;
      if (V.snap) yaw = snapTo(yaw, V.turnStep);
      g.rotation = [yaw, g.before.rotation[1], g.before.rotation[2]];
      if (g.group.length > 1) {
        const info = E.sceneInfo();
        for (const m of g.group) { const t = info.things[m.index]; E.thingSet(m.index, { rotation: [t.rotation[0] - (g.lastYaw ?? g.before.rotation[0]) + yaw, t.rotation[1], t.rotation[2]] }); }
        g.lastYaw = yaw;
      } else E.thingSet(g.sel.index, { rotation: g.rotation });
      hint('turned ' + S.num(((yaw * 180 / Math.PI) % 360 + 360) % 360) + '°', 0);
    } else if (g.kind === 'size') {
      let k = Math.max(0.05, Math.hypot(e.clientX - g.cx, e.clientY - g.cy) / g.r0);
      let s = g.before.scale.map((x) => x * k);
      if (V.snap) s = s.map((x) => Math.max(0.05, snapTo(x, 0.05)));
      g.scale = s;
      E.thingSet(g.sel.index, { scale: s });
      hint('size × ' + S.num(s[0] / (g.before.scale[0] || 1)), 0);
    }
  }
  function finishDrag(was) {
    const sel = was.sel;
    if (was.kind === 'move' || was.kind === 'lift') {
      const delta = sub(was.at, was.before.at);
      if (len(delta) < 1e-4) return hint('');
      const group = was.group.map((m) => ({ index: m.index, before: { at: m.at }, after: { at: add(m.at, delta) } }));
      writeBack('move', sel, was.before, { at: was.at }, group);
    } else if (was.kind === 'turn') {
      if (Math.abs(was.rotation[0] - was.before.rotation[0]) < 1e-4) return hint('');
      writeBack('turn', sel, was.before, { rotation: was.rotation }, was.group.length > 1 ? null : null);
    } else if (was.kind === 'size') {
      writeBack('size', sel, was.before, { scale: was.scale });
    }
    V.select(sel.index);
  }

  // ---------------------------------------------------------------- undo without running the script again
  // A move, turn or size goes back by putting the thing where it was; anything
  // else runs the script's text again.
  V.applyFast = (fast, dir) => {
    for (const f of fast) {
      const s = dir === 'undo' ? f.before : f.after;
      E.thingSet(f.index, s);
    }
    if (V.sel) V.select(V.sel.index, true);
  };

  // ---------------------------------------------------------------- walking (Play), as in the player
  function stick(el, onMove, isLook) {
    let id = null, cx = 0, cy = 0, px = 0, py = 0;
    const knob = el.querySelector('i');
    el.addEventListener('pointerdown', (e) => {
      id = e.pointerId; el.setPointerCapture(id);
      const r = el.getBoundingClientRect(); cx = r.left + r.width / 2; cy = r.top + r.height / 2; px = e.clientX; py = e.clientY;
    });
    el.addEventListener('pointermove', (e) => {
      if (e.pointerId !== id) return;
      let dx = e.clientX - cx, dy = e.clientY - cy;
      const d = Math.hypot(dx, dy), m = el.clientWidth * 0.45;
      if (d > m) { dx *= m / d; dy *= m / d; }
      knob.style.transform = `translate(${dx}px, ${dy}px)`;
      if (isLook) { onMove(e.clientX - px, e.clientY - py); px = e.clientX; py = e.clientY; } else onMove(dx / m, -dy / m);
    });
    const end = (e) => { if (e.pointerId !== id) return; id = null; knob.style.transform = ''; if (!isLook) onMove(0, 0); };
    el.addEventListener('pointerup', end); el.addEventListener('pointercancel', end);
  }
  stick($('#ls'), (x, y) => E.move(x, y), false);
  stick($('#rs'), (dx, dy) => { const k = LoopUI.setting(E.M, 'player.touchLook', 1.6); E.look(dx * k, dy * k); }, true);
  // a mouse drags to look while playing
  let looking = null;
  canvas.addEventListener('pointerdown', (e) => { if (V.mode === 'play' && e.pointerType === 'mouse') { looking = { x: e.clientX, y: e.clientY }; canvas.setPointerCapture(e.pointerId); } });
  canvas.addEventListener('pointermove', (e) => { if (looking) { E.look(e.clientX - looking.x, e.clientY - looking.y); looking = { x: e.clientX, y: e.clientY }; } });
  canvas.addEventListener('pointerup', () => { looking = null; });

  // ---------------------------------------------------------------- flying with keys (a computer)
  const held = new Set();
  V.keyDown = (code) => { held.add(code); };
  V.keyUp = (code) => { held.delete(code); };
  let lastFly = 0;
  function fly(now) {
    const dt = Math.min(0.05, (now - (lastFly || now)) / 1000);
    lastFly = now;
    if (V.mode !== 'edit' || !held.size) return;
    const f = norm(sub(V.cam.target, camAt())), flat = norm([f[0], 0, f[2]]), rt = norm(cross(flat, [0, 1, 0]));
    const speed = Math.max(1.5, V.cam.dist * 0.9) * (V.flySpeed || 1) * (held.has('ShiftLeft') || held.has('ShiftRight') ? 3 : 1) * dt;
    let m = [0, 0, 0];
    if (held.has('KeyW')) m = add(m, flat);
    if (held.has('KeyS')) m = sub(m, flat);
    if (held.has('KeyD')) m = add(m, rt);
    if (held.has('KeyA')) m = sub(m, rt);
    if (held.has('KeyE')) m = add(m, [0, 1, 0]);
    if (held.has('KeyQ')) m = sub(m, [0, 1, 0]);
    if (len(m) > 0) { V.cam.target = add(V.cam.target, mul(norm(m), speed)); V.pushCamera(); }
  }

  // ---------------------------------------------------------------- the overlay: outline, handles, marks
  // the light the overlay draws with: the editor's accent (white unless chosen otherwise)
  let LIGHT = '#ffffff', lightAt = 0;
  function light(now) {
    if (now - lightAt > 1000) { lightAt = now; LIGHT = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#ffffff'; }
    return LIGHT;
  }
  function circleHandle(x, y, kind, hot) {
    ctx.beginPath(); ctx.arc(x, y, HANDLE_R, 0, Math.PI * 2);
    ctx.shadowColor = 'rgba(255,255,255,0.55)'; ctx.shadowBlur = hot ? 18 : 10;
    ctx.fillStyle = hot ? LIGHT : 'rgba(0,0,0,0.82)'; ctx.fill();
    ctx.lineWidth = 1.6; ctx.strokeStyle = LIGHT; ctx.stroke();
    ctx.shadowBlur = 0;
    ctx.strokeStyle = hot ? '#000' : LIGHT; ctx.fillStyle = ctx.strokeStyle; ctx.lineWidth = 2.2; ctx.lineCap = 'round'; ctx.lineJoin = 'round';
    ctx.beginPath();
    if (kind === 'lift') { ctx.moveTo(x, y - 10); ctx.lineTo(x, y + 10); ctx.moveTo(x - 5, y - 5); ctx.lineTo(x, y - 10); ctx.lineTo(x + 5, y - 5); ctx.moveTo(x - 5, y + 5); ctx.lineTo(x, y + 10); ctx.lineTo(x + 5, y + 5); }
    else if (kind === 'turn') { ctx.arc(x, y, 8, -Math.PI * 0.9, Math.PI * 0.55); ctx.moveTo(x + 4.5, y + 9.5); ctx.lineTo(x - 0.5, y + 6.6); ctx.lineTo(x + 3.5, y + 2.5); }
    else if (kind === 'size') { ctx.moveTo(x - 8, y + 8); ctx.lineTo(x + 8, y - 8); ctx.moveTo(x + 2, y - 8); ctx.lineTo(x + 8, y - 8); ctx.lineTo(x + 8, y - 2); ctx.moveTo(x - 8, y + 2); ctx.lineTo(x - 8, y + 8); ctx.lineTo(x - 2, y + 8); }
    ctx.stroke();
  }
  const EDGES = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
  function draw(now) {
    fly(now);
    light(now);
    const dpr = Math.min(2, devicePixelRatio || 1);
    const cw = ov.clientWidth, ch = ov.clientHeight;
    if (ov.width !== Math.round(cw * dpr) || ov.height !== Math.round(ch * dpr)) { ov.width = Math.round(cw * dpr); ov.height = Math.round(ch * dpr); }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cw, ch);
    handles = [];
    if (!E.M || !E.ready || V.mode === 'play') return requestAnimationFrame(draw);
    const v = E.view();
    if (!v) return requestAnimationFrame(draw);
    VP = v.viewProj; W = cw; Hh = ch;
    // marks: sounds, shot cameras
    for (const m of V.extra) {
      const p = m.at && project(m.at);
      m._p = p;
      if (!p) continue;
      ctx.beginPath(); ctx.arc(p[0], p[1], 15, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(0,0,0,0.8)'; ctx.fill();
      ctx.strokeStyle = m.color || '#bdbdbd'; ctx.lineWidth = 1.5; ctx.stroke();
      if (m === V.selMark) { ctx.beginPath(); ctx.arc(p[0], p[1], 21, 0, Math.PI * 2); ctx.shadowColor = 'rgba(255,255,255,0.7)'; ctx.shadowBlur = 12; ctx.strokeStyle = LIGHT; ctx.lineWidth = 2; ctx.stroke(); ctx.shadowBlur = 0; }
      ctx.fillStyle = m.color || '#bdbdbd'; ctx.font = '600 11px -apple-system, system-ui, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText(m.glyph || '♪', p[0], p[1] + 0.5);
      if (m.label) { ctx.fillStyle = 'rgba(242,242,242,0.9)'; ctx.font = '600 11px -apple-system, system-ui, sans-serif'; ctx.fillText(m.label, p[0], p[1] + 27); }
    }
    if (V.sel && V.mode === 'edit') {
      const t = E.thing(V.sel.index);
      if (t && t.visible) {
        const cs = corners(t).map(project);
        ctx.lineWidth = 1.6; ctx.strokeStyle = LIGHT; ctx.shadowColor = 'rgba(255,255,255,0.85)'; ctx.shadowBlur = 10;
        ctx.beginPath();
        for (const [a, b] of EDGES) if (cs[a] && cs[b]) { ctx.moveTo(cs[a][0], cs[a][1]); ctx.lineTo(cs[b][0], cs[b][1]); }
        ctx.stroke();
        ctx.shadowBlur = 0;
        if (V.sel.line > 0) {
          const wb = worldBox(t), c = mul(add(wb.lo, wb.hi), 0.5);
          const top = project([c[0], wb.hi[1], c[2]]), base = project([c[0], wb.lo[1], c[2]]), mid = project(c);
          const pts = cs.filter(Boolean);
          if (top && base && mid && pts.length) {
            const maxX = Math.max(...pts.map((p) => p[0])), minX = Math.min(...pts.map((p) => p[0]));
            const minY = Math.min(...pts.map((p) => p[1])), maxY = Math.max(...pts.map((p) => p[1]));
            const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
            // round the thing, never on top of each other however small it looks
            const R = Math.max(46, Math.max(maxX - minX, maxY - minY) / 2);
            const rx = Math.max(maxX - cx, 34) + 30, ry = Math.max(cy - minY, 30) + 30;
            const clampX = (x) => Math.max(HANDLE_R + 4, Math.min(cw - HANDLE_R - 4, x)), clampY = (y) => Math.max(HANDLE_R + 4, Math.min(ch - HANDLE_R - 4, y));
            const liftP = { kind: 'lift', x: clampX(cx), y: clampY(cy - ry) };
            const turnP = { kind: 'turn', x: clampX(cx + rx), y: clampY(cy + Math.min(R * 0.55, ry)) };
            const sizeP = { kind: 'size', x: clampX(cx + rx), y: clampY(cy - Math.min(R * 0.55, ry)) };
            handles = V.sel.many ? [liftP, turnP] : [liftP, turnP, sizeP];
            for (const hd of handles) circleHandle(hd.x, hd.y, hd.kind, g && g.kind === hd.kind);
          }
        }
      }
    }
    requestAnimationFrame(draw);
  }
  requestAnimationFrame(draw);
})();
