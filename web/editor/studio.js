// The Studio: one thing opened and worked on (docs/STUDIO.md). Three benches:
//   Shape  its points: dots a thumb can take; drag one and the shape follows, let go and a
//          `pull` line goes under the box in the script (the engine draws the pull: loop_shape_*); or, with the
//          Faces tool, an arrow on each face you can see: drag it out (or in) and an `extrude` line goes under it
//   Bones  its skeleton and how the skin follows            (not built yet)
//   Move   keys on a strip of time: a key pins a value to a moment, and the
//          engine fills in every moment between
// On the Move bench the thing's clip is held at the white line's moment.
// Moving, turning or sizing the thing there keeps a key at that moment: no
// record button to forget. The keys are the thing's `clip` and `key` lines in
// the script; the engine writes those lines itself (loop_clip_script), so the
// editor and the engine never write a clip two ways. The world keeps running.
window.LE = window.LE || {};
(() => {
  const { h } = LE;
  const E = LE.engine, S = LE.source, H = LE.history, V = LE.view, SP = LE.spaces;
  const I = (n) => LE.icon(n);
  // a keyed value's name as a person reads it
  const ROWS = { at: 'Place', turn: 'Turn', tilt: 'Tilt', roll: 'Roll', rotation: 'Turn 3', scale: 'Size', visible: 'Seen' };
  const NAME_W = 58, ROW_H = 36, RULER_H = 26, HIT = 18;
  const ST = { open: false, bench: 'move', index: -1, line: 0, label: '', moment: 0, playing: false, key: null, span: 4, clip: null };
  LE.studio = ST;
  const EMPTY = { channels: [], length: 0, loop: 'once', rate: 30 };
  const clone = (o) => JSON.parse(JSON.stringify(o));
  // a clip as the Studio changes and keeps it: its keys without the slopes the engine reports beside them
  const pack = (c) => ({ channels: c.channels.map((ch) => ({ name: ch.name, size: ch.size, angle: ch.angle, keys: ch.keys.map((k) => ({ t: k.t, v: k.v.slice(), way: k.way, ...(k.in ? { in: k.in.slice() } : {}), ...(k.out ? { out: k.out.slice() } : {}) })) })), length: c.length, loop: c.loop, rate: c.rate });
  const clipOf = (index) => E.clips().find((c) => c.kind === 't' && c.target === (index ?? ST.index)) || null;
  const rate = () => (ST.clip && ST.clip.rate) || 30;
  const snap = (t) => Math.max(0, Math.round(t * rate()) / rate());
  const same = (a, b) => Math.abs(a - b) < 1e-4;
  const said = (t) => S.num(t) + ' s · frame ' + Math.round(t * rate());

  // Why a picked thing can't be opened ('' when it can).
  ST.why = (sel) => {
    if (!sel) return 'Pick a thing first.';
    if (sel.line <= 0) return sel.info.line > 0 ? 'This one is made in a shared script, so it is opened there.' : 'The engine made this one; it moves by itself.';
    if (sel.many) return 'This line makes several things (a repeat). The Studio opens one thing with a line of its own.';
    if (/^\s/.test(S.lineAt(H.text, sel.line))) return 'This thing is inside a group or a block. For now the Studio opens things on a line of their own; its keys can be written in Logic.';
    if (sel.info.solid) return 'A solid thing is moved by physics, so keys can\'t move it too.';
    return '';
  };

  // ---------------------------------------------------------------- in and out
  function hold() {
    ST.clip = clipOf();
    if (ST.clip) E.clipSet(ST.clip.index, { playing: false, moment: ST.moment });
    ST.playing = false;
  }
  ST.enter = (sel) => {
    const why = ST.why(sel);
    if (why) return LE.toast(why);
    Object.assign(ST, { open: true, bench: 'move', index: sel.index, line: sel.line, label: sel.name, moment: 0, key: null, span: 4 });
    hold();
    if (ST.clip) ST.span = Math.max(4, Math.ceil(ST.clip.duration + 1));
    V.intercept = onPlace;
    SP.show('build');
    V.hint('Slide the white line, then move the thing: each move is a key', 4500);
    requestAnimationFrame(tick);
  };
  // Done: the world takes the thing back. Its clip was held here, so the world
  // is built again from its text (the clip plays as its line says).
  ST.leave = (quiet) => {
    if (!ST.open) return;
    V.hook = null;
    const touched = !!ST.clip;
    ST.open = false; ST.playing = false; ST.key = null; ST.clip = null;
    V.intercept = null;
    if (touched) SP.rerun();
    if (!quiet) SP.redraw();
  };
  ST.reset = () => { ST.open = false; ST.playing = false; ST.key = null; ST.clip = null; V.intercept = null; V.hook = null; };
  // After the world is built again (an undo, a change in Logic): the same line's thing, held again.
  ST.refresh = () => {
    if (!ST.open) return;
    if (!V.selectLine(ST.line) || ST.why(V.sel)) { ST.reset(); SP.redraw(); return; }
    ST.index = V.sel.index;
    ST.label = V.sel.name;
    hold();
    if (ST.key && !findKey(ST.clip, ST.key)) ST.key = null;
  };
  // the Studio's thing stays picked; Done is how you leave
  let locking = false;
  V.on((kind, sel) => {
    if (kind === 'select' && ST.open && !locking && (!sel || sel.index !== ST.index)) { locking = true; V.select(ST.index, true); locking = false; }
    if (kind === 'mode' && sel === 'play' && ST.open) ST.leave();
  });

  // ---------------------------------------------------------------- changing the clip
  const findChannel = (m, name) => (m ? m.channels.find((c) => c.name === name) : null);
  function findKey(m, key) {
    const ch = findChannel(m, key.channel);
    return ch ? ch.keys.find((k) => same(k.t, key.t)) : null;
  }
  function putKey(m, name, size, angle, v, t) {
    let ch = findChannel(m, name);
    if (!ch) { ch = { name, size, angle, keys: [] }; m.channels.push(ch); }
    const k = ch.keys.find((x) => same(x.t, t));
    if (k) k.v = v.slice();
    else { ch.keys.push({ t, v: v.slice(), way: name === 'visible' ? 'hold' : 'smooth' }); ch.keys.sort((a, b) => a.t - b.t); }
  }
  // One change to the clip: the engine at once, the script's lines, and the history.
  // `things`: what the same change did to the thing itself, for undo without a rebuild.
  function edit(label, mutate, things) {
    if (ST.playing) pause();
    const c = clipOf();
    const before = c ? pack(c) : clone(EMPTY), model = clone(before);
    mutate(model);
    model.channels = model.channels.filter((ch) => ch.keys.length);
    if (JSON.stringify(model) === JSON.stringify(before)) return false;
    const named = S.nameThing(H.text, ST.line);
    if (!named) { LE.toast('This line can\'t be given a name, so it can\'t take keys. Write them in Logic.'); return false; }
    const idx = c ? c.index : E.clipSet(-1, { kind: 't', target: ST.index });
    if (idx < 0 || E.clipSet(idx, { ...model, playing: false, moment: ST.moment }) < 0) { LE.toast('The engine refused that change (Logic → Messages says why).'); return false; }
    const text = S.setClipBlock(named.text, named.name, ST.line, model.channels.length ? E.clipScript(idx, named.name, '') : '');
    // a value that lost its last key goes back to what the thing's own line says: build the world again
    const dropped = before.channels.some((b) => !findChannel(model, b.name));
    H.commit(text, label, dropped ? null : [{ clipOf: ST.line, before, after: model }, ...(things || [])]);
    ST.clip = clipOf();
    ST.span = Math.max(ST.span, Math.ceil((ST.clip ? ST.clip.duration : 0) + 1));
    if (LE.logic) LE.logic.sync();
    if (dropped) SP.rerun(); else SP.redraw();
    return true;
  }
  // Undo and redo of a clip change without building the world again (LE.view.applyFast).
  ST.applyFast = (f, dir) => {
    const info = E.sceneInfo(), want = f.clipOf + (E.lineOffset || 0);
    const ti = info && info.things ? info.things.findIndex((t) => t.line === want) : -1;
    if (ti < 0) return;
    const state = dir === 'undo' ? f.before : f.after;
    const c = clipOf(ti);
    const idx = c ? c.index : E.clipSet(-1, { kind: 't', target: ti });
    const held = ST.open && ST.index === ti;
    E.clipSet(idx, { ...state, playing: held ? false : (c ? c.starts : true), ...(held ? { moment: ST.moment } : {}) });
    if (held) { ST.clip = clipOf(); if (ST.key && !findKey(ST.clip, ST.key)) ST.key = null; }
  };

  // A move, turn or size made in the world while the Move bench is open: a key at the white line.
  function onPlace(kind, sel, before, after) {
    if (!ST.open || ST.bench !== 'move' || sel.index !== ST.index) return false;
    const t = ST.moment;
    const now = E.thing(ST.index);
    const full = (s) => ({ at: s.at || now.position, rotation: s.rotation || now.rotation, scale: s.scale || now.scale });
    const things = [{ index: ST.index, before: full(before), after: full(after) }];
    if (kind === 'move') edit('a key: ' + ST.label + ' placed at ' + S.num(t) + ' s', (m) => putKey(m, 'at', 3, false, after.at, t), things);
    else if (kind === 'turn') edit('a key: ' + ST.label + ' turned at ' + S.num(t) + ' s', (m) => putKey(m, 'turn', 1, true, [after.rotation[0]], t), things);
    else if (kind === 'size') edit('a key: ' + ST.label + ' sized at ' + S.num(t) + ' s', (m) => putKey(m, 'scale', 3, false, after.scale, t), things);
    ST.key = { channel: kind === 'move' ? 'at' : kind === 'turn' ? 'turn' : 'scale', t };
    SP.redraw();
    return true;
  }
  // Key here: every keyed value (or its place, when it has no keys yet) pinned as it is now.
  ST.keyHere = () => {
    const now = E.thing(ST.index), t = ST.moment;
    if (!now) return;
    const value = { at: now.position, turn: [now.rotation[0]], tilt: [now.rotation[1]], roll: [now.rotation[2]], rotation: now.rotation, scale: now.scale, visible: [now.visible ? 1 : 0] };
    const ok = edit('keys for ' + ST.label + ' at ' + S.num(t) + ' s', (m) => {
      if (!m.channels.length) return putKey(m, 'at', 3, false, now.position, t);
      for (const ch of clone(m.channels)) if (value[ch.name]) putKey(m, ch.name, ch.size, ch.angle, value[ch.name], t);
    });
    if (!ok) V.hint('It already has these keys at this moment', 2200);
  };
  ST.removeKey = () => {
    const key = ST.key;
    if (!key) return;
    ST.key = null;
    edit('removed a key of ' + ST.label, (m) => { const ch = findChannel(m, key.channel); if (ch) ch.keys = ch.keys.filter((k) => !same(k.t, key.t)); });
  };
  function retime(key, to) {
    if (same(key.t, to)) return;
    edit('moved a key of ' + ST.label + ' to ' + S.num(to) + ' s', (m) => {
      const ch = findChannel(m, key.channel);
      const k = ch && ch.keys.find((x) => same(x.t, key.t));
      if (!k) return;
      ch.keys = ch.keys.filter((x) => x === k || !same(x.t, to));  // it takes the place of a key already there
      k.t = to;
      ch.keys.sort((a, b) => a.t - b.t);
    });
    ST.key = { channel: key.channel, t: to };
    seek(to);
    SP.redraw();
  }
  function setLoop(loop) { edit(ST.label + ' plays ' + (loop === 'once' ? 'once' : loop === 'loop' ? 'round and round' : 'back and forth'), (m) => { m.loop = loop; }); }

  // ---------------------------------------------------------------- time
  let headEl = null, whenEl = null;
  function showMoment() {
    if (headEl && headEl.isConnected) headEl.style.left = (Math.min(ST.moment, ST.span) / ST.span * 100) + '%';
    if (whenEl && whenEl.isConnected) whenEl.textContent = S.num(ST.moment) + ' s';
  }
  function seek(t) {
    ST.moment = snap(Math.min(t, ST.span));
    if (ST.clip) E.clipSet(ST.clip.index, { moment: ST.moment, playing: false });
    showMoment();
  }
  function play() {
    const c = ST.clip = clipOf();
    if (!c || !c.channels.length) return V.hint('Nothing to play yet: give it two keys at two moments', 2600);
    if (c.loop === 'once' && ST.moment >= c.duration - 1e-6) ST.moment = 0;
    E.clipSet(c.index, { moment: ST.moment, playing: true, speed: 1 });
    ST.playing = true;
    SP.redraw();
    requestAnimationFrame(tick);
  }
  function pause() {
    ST.playing = false;
    if (ST.clip) { ST.moment = snap(E.clipMoment(ST.clip.index)); E.clipSet(ST.clip.index, { playing: false, moment: ST.moment }); }
  }
  ST.togglePlay = () => { if (ST.playing) { pause(); SP.redraw(); } else play(); };
  function tick() {
    if (!ST.open || !ST.playing || !ST.clip) return;
    ST.moment = E.clipMoment(ST.clip.index);
    showMoment();
    if (!E.clipPlaying(ST.clip.index)) { ST.playing = false; ST.moment = snap(ST.moment); SP.redraw(); return; }
    requestAnimationFrame(tick);
  }

  // ---------------------------------------------------------------- the sheet
  function toolBtn(icon, label, onclick, opt = {}) {
    return h('button', { class: 'tool' + (opt.on ? ' on' : '') + (opt.danger ? ' danger' : ''), onclick, disabled: opt.disabled, title: opt.title || label }, I(icon), label);
  }
  ST.draw = (body) => {
    body.className = '';
    body.innerHTML = '';
    headEl = whenEl = null;
    body.append(h('div', { class: 'sh-head' }, h('h2', {}, ST.label), h('span', { class: 'sub' }, 'in the Studio'), h('span', { style: 'flex:1' }),
      h('button', { class: 'pill primary', onclick: () => ST.leave(), title: 'Back to the world (Esc)' }, I('check'), 'Done')));
    const benches = h('div', { class: 'chips benches' });
    for (const [k, label] of [['shape', 'Shape'], ['bones', 'Bones'], ['move', 'Move']]) benches.append(h('button', { class: 'pill' + (ST.bench === k ? ' on' : ''), onclick: () => { if (ST.playing) pause(); ST.bench = k; SP.redraw(); } }, label));
    body.append(benches);
    if (ST.bench === 'shape') return drawShape(body);
    V.hook = null;
    if (ST.bench === 'bones') return body.append(h('div', { class: 'card' }, h('h3', {}, 'Bones'), h('div', { class: 'note', style: 'margin:0' }, 'Give it a skeleton and paint how its skin follows. Not built yet: it comes after Shape.')));
    drawMove(body);
  };


  // ---------------------------------------------------------------- the Shape bench
  // The thing's points are dots over the world, each a thumb wide (44 px to touch, a smaller one drawn). A drag
  // moves one on a plane facing the camera and the engine draws the shape at once; letting go writes the pull
  // as a line under the box, one undo step. The engine says the line itself (loop_shape_drop).
  const DOT_HIT = 22, DOT_R = 11;
  const canvasEl = () => document.querySelector('#view');
  const ARROW_PX = 46;   // a face's arrow, on the screen: long enough to aim, its knob a thumb wide
  let pulling = null;   // {name, plane point, normal, grab offset}, or {face, its middle, the way it faces, s0}
  ST.shape = null; ST.dots = []; ST.knobs = []; ST.shapeWhy = ''; ST.tool = ST.tool || 'points';
  // the points now, from the engine; the Studio's own copy for drawing and for the tests
  function openShape() {
    ST.shape = null; ST.dots = []; ST.shapeWhy = '';
    const call = S.callOf(S.lineAt(H.text, ST.line));
    if (!call || call.word !== 'box') ST.shapeWhy = 'Only a box can be pulled so far; its line is the recipe the pulls go under.';
    else {
      const sh = E.shapeOpen(ST.index);
      if (!sh || !sh.points.length) ST.shapeWhy = 'This thing\'s shape can\'t be opened.';
      else if (sh.points.length > 96) ST.shapeWhy = 'Too many points to show at once (' + sh.points.length + '). A rounded box has one on every curve; try a square-cornered one.';
      else ST.shape = sh;
    }
    V.hook = ST.shape ? SHAPE_HOOK : null;
  }
  // the steps already under the thing's line: [{n, text}]
  function stepLines() {
    const out = [], end = S.blockEnd(H.text, ST.line);
    for (let n = ST.line + 1; n <= end; n++) { const t = S.lineAt(H.text, n).trim(); if (t) out.push(t); }
    return out;
  }
  const SHAPE_HOOK = {
    quiet: true,   // the thing's own handles (lift, turn, size) step aside
    draw(ctx, project) {
      const sh = ST.shape;
      ST.dots = []; ST.knobs = [];
      if (!sh || !ST.open || ST.bench !== 'shape') return;
      if (ST.tool === 'faces') return drawFaces(ctx, project, sh);
      const at = new Map();
      for (const p of sh.points) { const q = project(p.at); if (q) { at.set(p.name, q); ST.dots.push({ name: p.name, x: q[0], y: q[1] }); } }
      ctx.lineWidth = 1.5; ctx.strokeStyle = 'rgba(255,255,255,0.55)';
      ctx.beginPath();
      for (const [a, b] of sh.edges) { const qa = at.get(a), qb = at.get(b); if (qa && qb) { ctx.moveTo(qa[0], qa[1]); ctx.lineTo(qb[0], qb[1]); } }
      ctx.stroke();
      ctx.font = '600 11px -apple-system, system-ui, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      for (const d of ST.dots) {
        const on = pulling && pulling.name === d.name;
        ctx.beginPath(); ctx.arc(d.x, d.y, DOT_HIT, 0, Math.PI * 2);   // the halo is the part a thumb can take
        ctx.fillStyle = on ? 'rgba(255,200,80,0.28)' : 'rgba(255,255,255,0.14)'; ctx.fill();
        ctx.beginPath(); ctx.arc(d.x, d.y, DOT_R, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(20,20,20,0.9)'; ctx.fill();
        ctx.lineWidth = 2.5; ctx.strokeStyle = on ? '#ffc850' : '#f2f2f2'; ctx.stroke();
        ctx.fillStyle = '#f2f2f2'; ctx.fillText(String(d.name), d.x, d.y + 0.5);
      }
    },
    down(e, ray) {
      if (!ST.shape || !ST.open || ST.bench !== 'shape' || !ray) return false;
      const r = canvasEl().getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top;
      if (ST.tool === 'faces') {
        let k = null, kd = DOT_HIT;
        for (const d of ST.knobs) { const dd = Math.hypot(d.x - x, d.y - y); if (dd < kd) { kd = dd; k = d; } }
        const f = k && ST.shape.faces.find((q) => q.name === k.name);
        if (!f || !E.shapeGrabFace(ST.index, f.name)) return false;
        pulling = { face: f.name, c: f.at.slice(), n: f.out.slice(), s0: alongLine(ray, f.at, f.out) };
        V.hint('Drag the arrow out to grow the face, in to dig; let go to keep it', 0);
        return true;
      }
      let best = null, bd = DOT_HIT;
      for (const d of ST.dots) { const dd = Math.hypot(d.x - x, d.y - y); if (dd < bd) { bd = dd; best = d; } }
      if (!best) return false;
      const pt = ST.shape.points.find((p) => p.name === best.name);
      if (!pt || !E.shapeGrab(ST.index, best.name)) return false;
      const n = unit(sub3(ray.v.look_at, ray.v.at));
      const hit = onPlane(ray, pt.at, n);
      pulling = { name: best.name, p0: pt.at.slice(), n, grab: hit ? sub3(pt.at, hit) : [0, 0, 0] };
      V.hint('Point ' + best.name + ': drag it, let go to keep it', 0);
      return true;
    },
    move(e, ray) {
      if (!pulling || !ray) return;
      if (pulling.face) {
        const t = alongLine(ray, pulling.c, pulling.n);
        if (t === null || pulling.s0 === null) return;
        const d = t - pulling.s0, c = pulling.c, n = pulling.n;
        E.shapeDrag(c[0] + n[0] * d, c[1] + n[1] * d, c[2] + n[2] * d);
        ST.shape = E.shapePoints(ST.index) || ST.shape;
        return;
      }
      const hit = onPlane(ray, pulling.p0, pulling.n);
      if (!hit) return;
      const to = [hit[0] + pulling.grab[0], hit[1] + pulling.grab[1], hit[2] + pulling.grab[2]];
      E.shapeDrag(to[0], to[1], to[2]);
      ST.shape = E.shapePoints(ST.index) || ST.shape;
    },
    up() {
      if (!pulling) return;
      const { name, face } = pulling;
      pulling = null;
      V.hint('');
      const line = E.shapeDrop();
      ST.shape = E.shapePoints(ST.index) || ST.shape;
      if (!line) return;
      const text = S.addStep(H.text, ST.line, line);
      if (text === null) return LE.toast('This line can\'t take a hand edit. Write it under the box in Logic.');
      // the engine already shows the change; undo builds the world again from the text
      H.commit(text, face ? 'extruded face ' + face + ' of ' + ST.label : 'pulled point ' + name + ' of ' + ST.label, null);
      if (LE.logic) LE.logic.sync();
      SP.redraw();
    },
  };
  const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const unit = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  // where a view ray meets the plane through p with normal n
  function onPlane(ray, p, n) {
    const den = ray.d[0] * n[0] + ray.d[1] * n[1] + ray.d[2] * n[2];
    if (Math.abs(den) < 1e-6) return null;
    const t = ((p[0] - ray.o[0]) * n[0] + (p[1] - ray.o[1]) * n[1] + (p[2] - ray.o[2]) * n[2]) / den;
    return t > 0 ? [ray.o[0] + ray.d[0] * t, ray.o[1] + ray.d[1] * t, ray.o[2] + ray.d[2] * t] : null;
  }
  // how far along the line through p (way n, unit) the view ray comes closest to it, or null when they run together
  function alongLine(ray, p, n) {
    const w = sub3(p, ray.o), b = n[0] * ray.d[0] + n[1] * ray.d[1] + n[2] * ray.d[2];
    const c = ray.d[0] * ray.d[0] + ray.d[1] * ray.d[1] + ray.d[2] * ray.d[2];
    const dn = n[0] * w[0] + n[1] * w[1] + n[2] * w[2], e = ray.d[0] * w[0] + ray.d[1] * w[1] + ray.d[2] * w[2];
    const den = c - b * b;
    return Math.abs(den) < 1e-6 ? null : (b * e - c * dn) / den;
  }
  // the Faces tool: the shape's edges, and on each face that looks towards you an arrow the way it faces
  function drawFaces(ctx, project, sh) {
    const at = new Map();
    for (const p of sh.points) { const q = project(p.at); if (q) at.set(p.name, q); }
    ctx.lineWidth = 1.5; ctx.strokeStyle = 'rgba(255,255,255,0.4)';
    ctx.beginPath();
    for (const [a, b] of sh.edges) { const qa = at.get(a), qb = at.get(b); if (qa && qb) { ctx.moveTo(qa[0], qa[1]); ctx.lineTo(qb[0], qb[1]); } }
    ctx.stroke();
    const v = E.view(), eye = v && v.at;
    for (const f of sh.faces || []) {
      const on = pulling && pulling.face === f.name;
      if (eye && !on) { const toEye = sub3(eye, f.at); if (toEye[0] * f.out[0] + toEye[1] * f.out[1] + toEye[2] * f.out[2] <= 0) continue; }
      const q0 = project(f.at), q1 = project([f.at[0] + f.out[0] * 0.1, f.at[1] + f.out[1] * 0.1, f.at[2] + f.out[2] * 0.1]);
      if (!q0 || !q1) continue;
      let dx = q1[0] - q0[0], dy = q1[1] - q0[1];
      const l = Math.hypot(dx, dy);
      if (l < 0.5) { dx = 0; dy = -1; } else { dx /= l; dy /= l; }   // a face seen straight on: its arrow points up
      const tip = [q0[0] + dx * ARROW_PX, q0[1] + dy * ARROW_PX];
      ST.knobs.push({ name: f.name, x: tip[0], y: tip[1], from: q0 });
      const col = on ? '#ffc850' : '#f2f2f2';
      ctx.lineWidth = 3; ctx.strokeStyle = col;
      ctx.beginPath(); ctx.moveTo(q0[0], q0[1]); ctx.lineTo(tip[0], tip[1]); ctx.stroke();
      ctx.beginPath(); ctx.arc(q0[0], q0[1], 4, 0, Math.PI * 2); ctx.fillStyle = col; ctx.fill();
      ctx.beginPath(); ctx.arc(tip[0], tip[1], DOT_HIT, 0, Math.PI * 2);
      ctx.fillStyle = on ? 'rgba(255,200,80,0.28)' : 'rgba(255,255,255,0.14)'; ctx.fill();
      ctx.beginPath(); ctx.arc(tip[0], tip[1], DOT_R, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(20,20,20,0.9)'; ctx.fill();
      ctx.lineWidth = 2.5; ctx.strokeStyle = col; ctx.stroke();
      ctx.beginPath(); ctx.moveTo(tip[0] + dx * 5, tip[1] + dy * 5); ctx.lineTo(tip[0] - dy * 4 - dx * 3, tip[1] + dx * 4 - dy * 3); ctx.lineTo(tip[0] + dy * 4 - dx * 3, tip[1] - dx * 4 - dy * 3); ctx.closePath();
      ctx.fillStyle = col; ctx.fill();
    }
  }
  function drawShape(body) {
    openShape();
    const card = h('div', { class: 'card' }, h('h3', {}, 'Shape'));
    if (!ST.shape) { card.append(h('div', { class: 'note', style: 'margin:0' }, ST.shapeWhy)); return body.append(card); }
    const tools = h('div', { class: 'row', style: 'gap:6px;margin:0 0 8px' });
    for (const [k, label] of [['points', 'Points'], ['faces', 'Faces']]) tools.append(h('button', { class: 'pill' + (ST.tool === k ? ' on' : ''), onclick: () => { ST.tool = k; SP.redraw(); } }, label));
    card.append(tools);
    if (ST.tool === 'faces') card.append(h('div', { class: 'note', style: 'margin:0' }, h('b', {}, 'Drag an arrow to extrude that face.'), ' Out grows a new block from it, in digs into the box. Let go and an extrude line is written under the box; one undo takes it back.'));
    else card.append(h('div', { class: 'note', style: 'margin:0' }, h('b', {}, 'Drag a dot to pull that point.'), ' ' + ST.shape.points.length + ' points. Let go and the pull is written under the box in the script; one undo takes it back.'));
    body.append(card);
    const steps = stepLines();
    const done = h('div', { class: 'card' }, h('h3', {}, 'Hand edits · ' + steps.length));
    if (!steps.length) done.append(h('div', { class: 'note', style: 'margin:0' }, 'None yet. The box is still just its recipe.'));
    for (const t of steps) done.append(h('div', { class: 'dim mono', style: 'padding:3px 0;overflow-wrap:anywhere' }, t));
    body.append(done);
  }

  function drawMove(body) {
    const c = ST.clip = clipOf();
    const channels = c ? c.channels : [];
    if (ST.key && !findKey(c, ST.key)) ST.key = null;
    body.append(h('div', { class: 'tools' },
      toolBtn(ST.playing ? 'pause' : 'play', ST.playing ? 'Pause' : 'Play', () => ST.togglePlay(), { on: ST.playing, title: 'Play this thing\'s keys (Space)' }),
      toolBtn('back', 'Start', () => { if (ST.playing) pause(); seek(0); SP.redraw(); }, { title: 'The white line back to the start' }),
      toolBtn('key', 'Key here', () => ST.keyHere(), { title: 'Pin its values as they are, at the white line\'s moment' }),
      toolBtn('delete', 'Remove key', () => ST.removeKey(), { danger: true, disabled: !ST.key, title: 'Remove the picked key (Delete)' }),
      toolBtn('plus', 'More time', () => { ST.span += 2; SP.redraw(); }, { title: 'Two more seconds on the strip' })));

    // ---- the strip of time: a ruler, a row for each keyed value, a diamond for each key
    const rows = channels.length;
    const strip = h('div', { class: 'kt', style: 'height:' + (RULER_H + Math.max(1, rows) * ROW_H) + 'px' });
    const names = h('div', { class: 'kt-names', style: 'width:' + NAME_W + 'px' });
    whenEl = h('div', { class: 'kt-when', style: 'height:' + RULER_H + 'px' }, '');
    names.append(whenEl);
    const tracks = h('div', { class: 'kt-tracks', style: 'left:' + (NAME_W + 10) + 'px' });
    const ruler = h('div', { class: 'kt-ruler', style: 'height:' + RULER_H + 'px' });
    const step = ST.span > 30 ? 5 : ST.span > 12 ? 2 : 1;
    for (let s = 0; s <= ST.span + 1e-6; s += step) ruler.append(h('span', { style: 'left:' + (s / ST.span * 100) + '%' }, s + 's'));
    tracks.append(ruler);
    const dots = [];
    // a diamond's place: the same fraction of the tracks' width as the white line's (a row starts 10 px left of the tracks)
    const place = (el, t) => { el.style.left = 'calc(10px + (100% - 22px) * ' + (t / ST.span) + ')'; };
    channels.forEach((ch, r) => {
      names.append(h('div', { class: 'kt-name', style: 'height:' + ROW_H + 'px' }, ROWS[ch.name] || ch.name));
      const row = h('div', { class: 'kt-row', style: 'top:' + (RULER_H + r * ROW_H) + 'px;height:' + ROW_H + 'px' });
      for (const k of ch.keys) {
        const on = ST.key && ST.key.channel === ch.name && same(ST.key.t, k.t);
        const d = h('i', { class: 'kd' + (on ? ' on' : '') + (k.way === 'hold' ? ' hold' : '') });
        dots.push({ el: d, channel: ch.name, row: r, t: k.t });
        place(d, k.t);
        row.append(d);
      }
      tracks.append(row);
    });
    if (!rows) tracks.append(h('div', { class: 'kt-empty', style: 'top:' + RULER_H + 'px;height:' + ROW_H + 'px' }, 'No keys yet'));
    headEl = h('div', { class: 'kt-head' });
    tracks.append(headEl);
    // one layer takes every touch: a diamond is picked (and dragged to another moment), anywhere else moves the white line
    const touch = h('div', { class: 'kt-touch' });
    const at = (e) => { const r = touch.getBoundingClientRect(); return { t: Math.max(0, Math.min(ST.span, (e.clientX - r.left) / r.width * ST.span)), x: e.clientX - r.left, y: e.clientY - r.top, w: r.width }; };
    let drag = null;
    touch.onpointerdown = (e) => {
      touch.setPointerCapture(e.pointerId);
      if (ST.playing) pause();
      const p = at(e), row = Math.floor((p.y - RULER_H) / ROW_H);
      let hit = null, best = HIT;
      for (const d of dots) { const dx = Math.abs(d.t / ST.span * p.w - p.x); if (d.row === row && dx < best) { best = dx; hit = d; } }
      if (hit) { drag = { kind: 'key', dot: hit, x0: e.clientX, to: hit.t, moved: false }; ST.key = { channel: hit.channel, t: hit.t }; seek(hit.t); dots.forEach((d) => d.el.classList.toggle('on', d === hit)); return; }
      drag = { kind: 'scrub', x0: e.clientX, moved: false };
      seek(p.t);
    };
    touch.onpointermove = (e) => {
      if (!drag) return;
      const p = at(e);
      if (Math.abs(e.clientX - drag.x0) > 6) drag.moved = true;
      if (drag.kind === 'scrub') return seek(p.t);
      if (!drag.moved) return;
      drag.to = snap(p.t);
      place(drag.dot.el, drag.to);
      V.hint('this key at ' + said(drag.to), 0);
    };
    touch.onpointerup = touch.onpointercancel = () => {
      const was = drag;
      drag = null;
      if (!was) return;
      if (was.kind === 'key' && was.moved) { V.hint(''); return retime({ channel: was.dot.channel, t: was.dot.t }, was.to); }
      if (was.kind === 'scrub' && !was.moved && ST.key) ST.key = null;  // a tap on empty time lets the key go
      SP.redraw();
    };
    tracks.append(touch);
    strip.append(names, tracks);
    body.append(strip);
    showMoment();

    if (!rows) body.append(h('div', { class: 'note' }, h('b', {}, 'A key pins a value to a moment.'), ' Move ', ST.label, ' where it should be now. Then slide the white line to another moment and move it again. The engine fills in every moment between.'));
    // ---- the picked key
    const k = ST.key && findKey(c, ST.key);
    if (k) {
      const ch = findChannel(c, ST.key.channel);
      const shown = ch.name === 'visible' ? (k.v[0] > 0.5 ? 'seen' : 'hidden') : k.v.map((x) => (ch.angle ? S.num(x * 180 / Math.PI) + '°' : S.num(x))).join(', ');
      const card = h('div', { class: 'card' }, h('h3', {}, 'Key · ' + (ROWS[ch.name] || ch.name) + ' · ' + said(k.t)));
      card.append(h('div', { class: 'field' }, h('span', {}, 'Value'), h('span', { class: 'dim mono' }, shown)));
      card.append(h('div', { class: 'field' }, h('span', {}, 'Moment'), h('div', { class: 'row' },
        h('button', { class: 'pill', disabled: k.t <= 0, onclick: () => retime(ST.key, snap(k.t - 1 / rate())) }, '−1 frame'),
        h('button', { class: 'pill', onclick: () => retime(ST.key, snap(k.t + 1 / rate())) }, '+1 frame'))));
      if (LE.curves) LE.curves.draw(card, ST, { clip: c, channel: ch, key: k, edit, seek, redraw: () => SP.redraw() });
      body.append(card);
    }
    // ---- how it repeats
    if (c && rows) {
      const loop = h('div', { class: 'card' }, h('h3', {}, 'When it reaches the end'));
      const chips = h('div', { class: 'chips' });
      for (const [v, label] of [['once', 'Stops'], ['loop', 'Starts again'], ['back and forth', 'Goes back']]) chips.append(h('button', { class: 'pill' + (c.loop === v ? ' on' : ''), onclick: () => setLoop(v) }, label));
      loop.append(chips);
      body.append(loop);
    }
  }
})();
