// The curve under your thumb (the Studio's Move bench, web/editor/studio.js).
// For the picked key: how its value arrives (held, straight, smooth, free, or
// an ease by name), and that value's curve drawn large. The curve's points
// come from the engine itself (loop_clip_curve), so what is drawn here is what
// plays and what film renders. A free key has two handles, sized for a thumb:
// they set how fast the value arrives and leaves. One finger on the curve
// slides the white line; two fingers slide and pinch the view.
window.LE = window.LE || {};
(() => {
  const { h } = LE;
  const E = LE.engine, S = LE.source, V = LE.view;
  const WAYS = [['hold', 'Held', 'Stays where it was, then jumps here'], ['line', 'Straight', 'Straight here at one speed'],
    ['smooth', 'Smooth', 'Through the keys with no overshoot: starts gently, lands softly'], ['free', 'Free', 'Its own handles: you set how fast it arrives and leaves']];
  const EASES = [['in', 'Slow start'], ['out', 'Slow stop'], ['in_out', 'Slow both'], ['back', 'Past and back'], ['bounce', 'Bounce'], ['elastic', 'Elastic'], ['spring', 'Spring']];
  const PARTS = { at: ['X', 'Y', 'Z'], scale: ['X', 'Y', 'Z'], rotation: ['Turn', 'Tilt', 'Roll'] };
  const HANDLE_R = 13, HANDLE_HIT = 26, H_PX = 170;
  const same = (a, b) => Math.abs(a - b) < 1e-4;
  const C = { part: {}, win: null, winFor: '', split: false };
  LE.curves = C;

  const keyIn = (m, name, t) => { const ch = m.channels.find((c) => c.name === name); return ch ? ch.keys.find((k) => same(k.t, t)) : null; };
  // how the picked key arrives, as one of the four buttons or an ease's name
  const wayOf = (k) => (k.in || k.out ? 'free' : k.way);
  // the slopes a smooth curve would have at a key, whatever way it arrives now: the engine's own
  // (each key of loop_clips says its slopes), rounded as the script keeps them
  function smoothSlopes(clip, ch, t) {
    const chans = JSON.parse(JSON.stringify(clip.channels));
    const key = keyIn({ channels: chans }, ch.name, t);
    delete key.in; delete key.out;
    key.way = 'smooth';
    E.clipSet(clip.index, { channels: chans });
    const now = E.clips().find((c) => c.index === clip.index), k = now && keyIn(now, ch.name, t);
    E.clipSet(clip.index, { channels: clip.channels });
    const round = (a) => a.map((x) => Math.round(x * 1000) / 1000);
    return k ? { in: round(k.slopeIn), out: round(k.slopeOut) } : { in: key.v.map(() => 0), out: key.v.map(() => 0) };
  }

  C.draw = (card, ST, ctx) => {
    const { clip, channel: ch, key: k } = ctx;
    const i = ch.keys.findIndex((x) => same(x.t, k.t)), prev = ch.keys[i - 1], next = ch.keys[i + 1];
    const now = wayOf(k);
    const setWay = (way) => {
      const s = way === 'free' ? smoothSlopes(clip, ch, k.t) : null;  // a free key starts as the smooth curve it was: nothing jumps
      ctx.edit('a key of ' + ST.label + ' arrives ' + (way === 'hold' ? 'held' : way === 'line' ? 'straight' : way), (m) => {
        const key = keyIn(m, ch.name, k.t);
        if (!key) return;
        delete key.in; delete key.out;
        if (s) { key.way = 'smooth'; key.in = s.in; key.out = s.out; } else key.way = way;
      });
    };
    // ---- how it arrives
    if (ch.name !== 'visible') {
      const ways = h('div', { class: 'ways' });
      for (const [way, label, title] of WAYS) ways.append(h('button', { class: 'way' + (now === way ? ' on' : ''), title, onclick: () => setWay(way) }, LE.icon('way-' + way), label));
      card.append(h('div', { class: 'way-head' }, 'How it arrives here'), ways);
      const eases = h('div', { class: 'chips' });
      for (const [e, label] of EASES) eases.append(h('button', { class: 'pill' + (now === e ? ' on' : ''), onclick: () => setWay(e) }, label));
      card.append(eases);
      if (!prev && clip.loop !== 'loop') card.append(h('div', { class: 'note' }, 'This is its first key, so nothing arrives here yet: this shows when it loops, or when a key is put before it.'));
    }
    if (ch.keys.length < 2) return;

    // ---- which number of a three-number value the big curve shows
    const names = PARTS[ch.name];
    let part = Math.min(C.part[ch.name] ?? -1, ch.size - 1);
    if (part < 0) {
      // the one that changes most round this key
      const span = (c) => Math.max(prev ? Math.abs(k.v[c] - prev.v[c]) : 0, next ? Math.abs(next.v[c] - k.v[c]) : 0);
      part = 0;
      for (let c = 1; c < ch.size; c++) if (span(c) > span(part)) part = c;
    }
    if (names && ch.size > 1) {
      const pick = h('div', { class: 'chips', style: 'margin-top:8px' });
      names.forEach((n, c) => pick.append(h('button', { class: 'pill' + (c === part ? ' on' : ''), onclick: () => { C.part[ch.name] = c; ctx.redraw(); } }, n)));
      card.append(pick);
    }

    // ---- the view: from the key before to the key after, unless slid or pinched
    const id = ST.line + ':' + ch.name + ':' + S.num(k.t) + ':' + ch.keys.length;
    if (C.winFor !== id) {
      const a = prev ? prev.t : Math.max(0, k.t - 0.5), b = next ? next.t : k.t + 0.5, pad = (b - a) * 0.12;
      C.win = [Math.max(-pad, a - pad), b + pad];
      C.winFor = id;
    }
    const canvas = h('canvas', { class: 'curve', style: 'height:' + H_PX + 'px' });
    card.append(canvas);
    const unit = (x) => (ch.angle ? S.num(x * 180 / Math.PI) + '°' : S.num(x));
    let live = null;  // the key's slopes while a handle is dragged: {in, out}
    let geom = null;

    function paint() {
      const dpr = Math.min(2, devicePixelRatio || 1), w = canvas.clientWidth || 300, hh = H_PX;
      if (canvas.width !== Math.round(w * dpr)) { canvas.width = Math.round(w * dpr); canvas.height = Math.round(hh * dpr); }
      const g = canvas.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, hh);
      const [t0, t1] = C.win, N = Math.max(48, Math.min(240, Math.round(w / 2)));
      const rows = E.clipCurve(clip.index, ch.name, t0, t1, N);
      if (!rows.length) return;
      const free = live || (k.in || k.out ? { in: k.in || k.out, out: k.out || k.in } : null);
      // the handles' reach: a third of the way to the key on each side
      const dIn = prev ? (k.t - prev.t) / 3 : (t1 - t0) / 8, dOut = next ? (next.t - k.t) / 3 : (t1 - t0) / 8;
      let lo = Infinity, hi = -Infinity;
      for (const r of rows) { lo = Math.min(lo, r[part]); hi = Math.max(hi, r[part]); }
      if (free) { lo = Math.min(lo, k.v[part] - free.in[part] * dIn, k.v[part] + free.out[part] * dOut); hi = Math.max(hi, k.v[part] - free.in[part] * dIn, k.v[part] + free.out[part] * dOut); }
      if (hi - lo < 1e-6) { lo -= 0.5; hi += 0.5; }
      let padV = (hi - lo) * 0.16;
      lo -= padV; hi += padV;
      // while a handle is held the view holds still (a view that grew with the handle would run away from the thumb)
      if (drag && drag.side && drag.view) { lo = drag.view.lo; hi = drag.view.hi; padV = drag.view.padV; }
      const X = (t) => (t - t0) / (t1 - t0) * w, Y = (v) => hh - (v - lo) / (hi - lo) * hh;
      geom = C.shown = { w, hh, t0, t1, lo, hi, X, Y, dIn, dOut, handles: [] };  // C.shown: for the editor's browser check
      // each key's moment, and the value's range
      g.lineWidth = 1; g.strokeStyle = 'rgba(255,255,255,0.09)';
      g.beginPath();
      for (const key of ch.keys) if (key.t >= t0 && key.t <= t1) { g.moveTo(X(key.t), 0); g.lineTo(X(key.t), hh); }
      g.stroke();
      g.fillStyle = 'rgba(255,255,255,0.4)'; g.font = '10px ui-monospace, Menlo, monospace'; g.textBaseline = 'top'; g.textAlign = 'left';
      g.fillText(unit(hi - padV), 6, 4);
      g.textBaseline = 'bottom';
      g.fillText(unit(lo + padV), 6, hh - 4);
      // the other numbers of the value, faint; then the one shown, bright
      const line = (c, style, width, glow) => {
        g.beginPath();
        rows.forEach((r, n) => { const x = n / (rows.length - 1) * w, y = Y(r[c]); if (n) g.lineTo(x, y); else g.moveTo(x, y); });
        g.strokeStyle = style; g.lineWidth = width; g.shadowColor = 'rgba(255,255,255,0.7)'; g.shadowBlur = glow; g.stroke(); g.shadowBlur = 0;
      };
      line(part, '#ffffff', 2.2, 8);
      // the white line's moment
      if (ST.moment >= t0 && ST.moment <= t1) { g.fillStyle = 'rgba(255,255,255,0.55)'; g.fillRect(X(ST.moment) - 0.75, 0, 1.5, hh); }
      // the keys
      for (const key of ch.keys) {
        if (key.t < t0 || key.t > t1) continue;
        const x = X(key.t), y = Y(key.v[part]), on = key === k, r = on ? 7 : 5;
        g.save(); g.translate(x, y); g.rotate(Math.PI / 4);
        g.fillStyle = on ? '#fff' : '#8f8f8f'; g.strokeStyle = '#000'; g.lineWidth = 1;
        g.fillRect(-r, -r, 2 * r, 2 * r); g.strokeRect(-r, -r, 2 * r, 2 * r);
        g.restore();
      }
      // a free key's two handles
      if (free) {
        const kx = X(k.t), ky = Y(k.v[part]);
        const ends = [];
        if (prev || clip.loop === 'loop') ends.push({ side: 'in', x: X(k.t - dIn), y: Y(k.v[part] - free.in[part] * dIn) });
        if (next || clip.loop === 'loop') ends.push({ side: 'out', x: X(k.t + dOut), y: Y(k.v[part] + free.out[part] * dOut) });
        for (const e of ends) {
          g.strokeStyle = 'rgba(255,255,255,0.7)'; g.lineWidth = 1.4;
          g.beginPath(); g.moveTo(kx, ky); g.lineTo(e.x, e.y); g.stroke();
          g.beginPath(); g.arc(e.x, e.y, HANDLE_R, 0, Math.PI * 2);
          g.fillStyle = drag && drag.side === e.side ? '#fff' : 'rgba(0,0,0,0.85)'; g.fill();
          g.strokeStyle = '#fff'; g.lineWidth = 1.8; g.shadowColor = 'rgba(255,255,255,0.6)'; g.shadowBlur = 10; g.stroke(); g.shadowBlur = 0;
        }
        geom.handles = ends;
      }
    }

    // ---- touches: a handle, the white line, or two fingers on the view
    const pts = new Map();
    let drag = null, two = null;
    const local = (e) => { const r = canvas.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
    const timeAt = (x) => geom.t0 + x / geom.w * (geom.t1 - geom.t0);
    const valueAt = (y) => geom.lo + (geom.hh - y) / geom.hh * (geom.hi - geom.lo);
    // the slopes while a handle is held, shown at once in the engine (kept in the script on letting go)
    function preview(slopes) {
      const chans = JSON.parse(JSON.stringify(clip.channels));
      const key = keyIn({ channels: chans }, ch.name, k.t);
      key.in = slopes.in; key.out = slopes.out;
      E.clipSet(clip.index, { channels: chans });
    }
    canvas.onpointerdown = (e) => {
      canvas.setPointerCapture(e.pointerId);
      pts.set(e.pointerId, local(e));
      if (pts.size === 2) {
        if (drag && drag.side) { E.clipSet(clip.index, { channels: clip.channels }); live = null; }
        drag = null;
        const [a, b] = [...pts.values()];
        two = { d: Math.max(20, Math.abs(a.x - b.x)), mid: (a.x + b.x) / 2, win: C.win.slice() };
        return;
      }
      if (!geom) return;
      const p = local(e);
      let hit = null, best = HANDLE_HIT;
      for (const hd of geom.handles) { const d = Math.hypot(hd.x - p.x, hd.y - p.y); if (d < best) { best = d; hit = hd; } }
      if (hit) {
        const s = { in: (k.in || k.out).slice(), out: (k.out || k.in).slice() };
        drag = { side: hit.side, start: s, view: { lo: geom.lo, hi: geom.hi, padV: (geom.hi - geom.lo) * 0.16 / 1.32 } };
        live = { in: s.in.slice(), out: s.out.slice() };
        paint();
        return;
      }
      drag = { side: null };
      ctx.seek(Math.max(0, timeAt(p.x)));
      paint();
    };
    canvas.onpointermove = (e) => {
      if (!pts.has(e.pointerId)) return;
      const p = local(e);
      pts.set(e.pointerId, p);
      if (two && pts.size === 2) {
        const [a, b] = [...pts.values()];
        const d = Math.max(20, Math.abs(a.x - b.x)), mid = (a.x + b.x) / 2;
        const span0 = two.win[1] - two.win[0], span = Math.max(0.1, Math.min(120, span0 * two.d / d));
        const anchor = two.win[0] + two.mid / geom.w * span0;  // the moment under the fingers stays under them
        C.win = [anchor - mid / geom.w * span, anchor - mid / geom.w * span + span];
        return paint();
      }
      if (!drag || !geom) return;
      if (!drag.side) { ctx.seek(Math.max(0, timeAt(p.x))); return paint(); }
      // a handle follows the thumb up and down; its reach along time is fixed
      const dt = drag.side === 'in' ? -geom.dIn : geom.dOut;
      const slope = (valueAt(p.y) - k.v[part]) / dt;
      live[drag.side][part] = slope;
      if (!C.split) live[drag.side === 'in' ? 'out' : 'in'][part] = slope;  // one straight line through the key, unless split
      preview(live);
      paint();
      V.hint((drag.side === 'in' ? 'arrives at ' : 'leaves at ') + unit(slope) + ' a second', 0);
    };
    canvas.onpointerup = canvas.onpointercancel = (e) => {
      pts.delete(e.pointerId);
      if (two) { if (pts.size < 2) two = null; return; }
      const was = drag;
      drag = null;
      if (!was || !was.side) return;
      V.hint('');
      const slopes = live;
      live = null;
      E.clipSet(clip.index, { channels: clip.channels });  // back to what the script holds, then the change is made as one
      ctx.edit('shaped how a key of ' + ST.label + ' arrives and leaves', (m) => { const key = keyIn(m, ch.name, k.t); if (key) { key.way = 'smooth'; key.in = slopes.in; key.out = slopes.out; } });
    };
    if (now === 'free') {
      card.append(h('div', { class: 'field', style: 'margin-top:8px' }, h('span', {}, 'Handles'), h('div', { class: 'row' },
        h('button', { class: 'pill' + (C.split ? '' : ' on'), onclick: () => { C.split = false; ctx.redraw(); } }, 'Together'),
        h('button', { class: 'pill' + (C.split ? ' on' : ''), onclick: () => { C.split = true; ctx.redraw(); } }, 'Each its own'))));
    }
    card.append(h('div', { class: 'note', style: 'margin-bottom:0' }, 'One finger slides the white line along the curve; two fingers slide and pinch the view.' + (now === 'free' ? ' Drag a round handle up or down to change how fast it arrives or leaves.' : '')));
    requestAnimationFrame(paint);
  };
})();
