// The four spaces. Each shows its tools in the sheet; none changes what a
// gesture means in the world. Every change goes through the script's text
// (LE.source) and the history (LE.history), then the world shows it.
//   Build  pick, move, add, copy, remove; where it is, its material; the world's list
//   Logic  the script, coloured and checked; its problems on their lines
//   Film   the shots on a timeline; a shot from where the view is; the look
//   Sound  the world's sounds, placed where they come from; the room's echo
window.LE = window.LE || {};
(() => {
  const { h } = LE;
  const E = LE.engine, S = LE.source, H = LE.history, V = LE.view;
  const I = (n) => LE.icon(n);
  const body = document.getElementById('sheetBody');
  const SP = { current: null, info: null };
  LE.spaces = SP;

  // a change that needs the world built again from its text
  SP.apply = async (text, label) => {
    if (!H.commit(text, label)) return;
    await SP.rerun();
  };
  SP.rerun = () => E.runText(H.text, E.scene);
  SP.refreshInfo = () => { const i = E.sceneInfo(); SP.info = i && i.things ? i : null; return SP.info; };

  // Where a new thing goes: on the floor in the middle of the view.
  function spotAhead(y) {
    const v = E.view();
    if (!v) return [0, y, 0];
    const f = [v.look_at[0] - v.at[0], v.look_at[1] - v.at[1], v.look_at[2] - v.at[2]];
    const L = Math.hypot(...f) || 1;
    // where the view's centre meets the floor, unless that's far off (a view
    // along the floor): then a few metres ahead, so it's easy to see and grab
    let t = f[1] < -1e-3 ? (0 - v.at[1]) / f[1] : Infinity;
    const near = Math.min(6, Math.max(2.5, V.cam.dist * 0.6));
    if (!(t > 0) || t * L > near * 1.6) {
      const flat = Math.hypot(f[0], f[2]) || 1;
      return [Math.round((v.at[0] + f[0] / flat * near) * 20) / 20, y, Math.round((v.at[2] + f[2] / flat * near) * 20) / 20];
    }
    const p = [v.at[0] + f[0] * t, 0, v.at[2] + f[2] * t];
    return [Math.round(p[0] * 20) / 20, y, Math.round(p[2] * 20) / 20];
  }
  // After the last line that makes a thing (so new things sit with the others).
  function lastThingLine() {
    const info = SP.info || SP.refreshInfo();
    let last = 0;
    if (info) for (const t of info.things) { const l = t.line - (E.lineOffset || 0); if (l > last) last = l; }
    if (!last) { const L = H.text.split('\n'); last = L.length; while (last > 0 && L[last - 1].trim() === '') last--; }
    return last;
  }
  async function addLine(line, label, selectIt) {
    const at = lastThingLine();
    const text = S.insertAfter(H.text, at, line);
    if (!H.commit(text, label)) return;
    SP.pendingSelect = selectIt ? at + 1 : null;
    await SP.rerun();
  }

  // ================================================================ Build
  const ADD = [
    { k: 'box', icon: 'box', label: 'Box', line: (p) => 'box size: vec(1, 1, 1), at: ' + S.vec([p[0], 0.5, p[2]]) + ', material: "oak"' },
    { k: 'ball', icon: 'sphere', label: 'Ball', line: (p) => 'sphere 0.5, at: ' + S.vec([p[0], 0.5, p[2]]) + ', material: "marble"' },
    { k: 'cyl', icon: 'cylinder', label: 'Cylinder', line: (p) => 'cylinder 0.4, 1, at: ' + S.vec([p[0], 0.5, p[2]]) + ', material: "stone"' },
    { k: 'cone', icon: 'cone', label: 'Cone', line: (p) => 'cone 0.5, 1, at: ' + S.vec([p[0], 0.5, p[2]]) + ', material: "clay"' },
    { k: 'wall', icon: 'part', label: 'Wall', line: (p) => 'box size: vec(4, 3, 0.2), at: ' + S.vec([p[0], 1.5, p[2]]) + ', material: "plaster"' },
    { k: 'tree', icon: 'tree', label: 'Tree', line: (p) => 'tree kind: "oak", at: ' + S.vec([p[0], 0, p[2]]) + ', height: 6' },
    { k: 'person', icon: 'person', label: 'Person', line: (p) => 'person at: ' + S.vec([p[0], 0, p[2]]) },
    { k: 'lamp', icon: 'light', label: 'Lamp', line: (p) => 'light at: ' + S.vec([p[0], 2.5, p[2]]) + ', lumens: 800, kelvin: 3000', mark: true },
  ];
  const MATERIALS = ['oak', 'walnut', 'pine', 'floorboards', 'stone', 'brick', 'marble', 'concrete', 'plaster', 'tiles', 'gold', 'brass', 'copper', 'steel', 'chrome', 'glass', 'water', 'velvet', 'leather', 'plastic', 'neon', 'white', 'black', 'grey'];
  const COLOURS = [[0.9, 0.9, 0.88], [0.12, 0.12, 0.12], [0.8, 0.2, 0.15], [0.95, 0.6, 0.15], [0.95, 0.85, 0.3], [0.3, 0.65, 0.3], [0.2, 0.45, 0.85], [0.5, 0.3, 0.75], [0.9, 0.45, 0.65], [0.45, 0.32, 0.22]];
  const rgbCss = (c) => 'rgb(' + c.map((x) => Math.round(x * 255)).join(',') + ')';

  // ---- the library: what every project can use (library/, scenes/library.json)
  SP.library = [];
  fetch('scenes/library.json').then((r) => (r.ok ? r.json() : [])).then((l) => { SP.library = l; if (SP.current === 'build' && !V.sel) SP.redraw(); }).catch(() => {});
  const KIND_ICON = { model: 'kind', room: 'top', piece: 'part' };
  // A library entry into the scene: its `use` line at the top (once), then its
  // example placed where the view looks (AT in its card's place line).
  async function addFromLibrary(e) {
    const p = spotAhead(0);
    const useLine = 'use "' + e.name + '"';
    let text = H.text, shift = 0;
    if (!text.split('\n').some((l) => l.trim() === useLine)) {
      // after the title, mode and other use lines at the top
      const L = text.split('\n');
      let at = 0;
      for (let k = 0; k < L.length && /^(#|title\b|mode\b|use\b|\s*$)/.test(L[k]); k++) if (/^(title|mode|use)\b/.test(L[k])) at = k + 1;
      text = S.insertAfter(text, at, useLine);
      shift = 1;
    }
    const line = e.place.replace(/AT/g, S.vec([p[0], p[1], p[2]]));
    text = S.insertAfter(text, lastThingLine() + shift, line);
    if (!H.commit(text, 'added a ' + e.name.replace(/_/g, ' ') + ' from the library')) return;
    SP.pendingSelect = null;
    await SP.rerun();
  }

  function toolBtn(icon, label, onclick, opt = {}) {
    return h('button', { class: 'tool' + (opt.on ? ' on' : '') + (opt.danger ? ' danger' : ''), onclick, disabled: opt.disabled, title: opt.title || label }, I(icon), label);
  }
  function numField(label, value, onchange, step = 0.05) {
    const inp = h('input', { type: 'number', step, value: S.num(value), inputmode: 'decimal' });
    inp.onchange = () => { if (inp.value !== '' && !isNaN(+inp.value)) onchange(+inp.value); };
    return h('label', {}, h('b', {}, label), inp);
  }

  const build = {
    title: 'Build', icon: 'cube-add',
    draw() {
      if (LE.studio && LE.studio.open) return LE.studio.draw(body);
      body.className = '';
      body.innerHTML = '';
      const sel = V.sel, mk = V.selMark;
      if (mk) return this.drawMark(mk);
      const head = h('div', { class: 'sh-head' }, h('h2', {}, sel ? sel.name : 'Build'), h('span', { class: 'sub' }, sel ? (sel.line > 0 ? 'line ' + sel.line : '') : 'Tap a thing to pick it'));
      body.append(head);
      if (!sel) {
        const tools = h('div', { class: 'tools' });
        for (const a of ADD) tools.append(toolBtn(a.icon, a.label, () => addLine(a.line(spotAhead(0)), 'added a ' + a.label.toLowerCase(), !a.mark)));
        const view = h('div', { class: 'tools' },
          toolBtn('light', V.workLight ? 'Work light on' : 'Work light', () => { V.setWorkLight(!V.workLight); this.draw(); }, { on: V.workLight, title: 'A lamp at your view, to see while building in the dark. Only in the editor: the world doesn\'t change.' }),
          toolBtn('grid', V.snap ? 'Snap on' : 'Snap off', () => { V.snap = !V.snap; this.draw(); }, { on: V.snap, title: 'Moves snap to 5 cm and turns to 15°' }));
        body.append(tools, view, h('div', { class: 'note' }, 'New things land on the floor in the middle of the view. ', h('b', {}, 'Drag'), ' a picked thing to slide it, use its handles to lift, turn and size it.'));
        if (SP.library.length) {
          const lib = h('div', { class: 'tools library' });
          for (const e of SP.library) lib.append(toolBtn(KIND_ICON[e.kind] || 'layers', e.name.replace(/_/g, ' '), () => addFromLibrary(e), { title: e.what + ' (from ' + e.from + ')' }));
          body.append(h('div', { class: 'sh-head' }, h('h2', {}, 'From the library'), h('span', { class: 'sub' }, SP.library.length + ' pieces every project shares')), lib);
        }
        body.append(this.worldCard(), this.listCard());
        return;
      }
      const t = sel.info;
      const tools = h('div', { class: 'tools' },
        toolBtn('studio', 'Open', () => LE.studio.enter(V.sel), { title: 'Open it in the Studio: its shape, its bones and its movement (keys)' }),
        toolBtn('duplicate', 'Copy', () => this.duplicate(), { disabled: sel.line <= 0 }),
        toolBtn('delete', 'Remove', () => this.remove(), { danger: true, disabled: sel.line <= 0 }),
        toolBtn('target', 'Frame', () => V.frame()),
        toolBtn('code', 'Its line', () => { SP.show('logic'); LE.logic.gotoLine(sel.line); }, { disabled: sel.line <= 0 }),
        toolBtn('grid', V.snap ? 'Snap on' : 'Snap off', () => { V.snap = !V.snap; this.draw(); }, { on: V.snap, title: 'Moves snap to 5 cm and turns to 15°' }),
        toolBtn('x', 'Let go', () => V.select(null)));
      body.append(tools);
      if (sel.line <= 0) {
        body.append(h('div', { class: 'note' }, t.line > 0 ? 'This thing is made in a shared script (use "…"), so it\'s changed there.' : 'The engine made this one (part of a person or a creature); it moves by itself.'));
        return;
      }
      if (sel.many) body.append(h('div', { class: 'note' }, 'This line makes ', h('b', {}, t.lineCount + ' things'), ' (a repeat). Moving one moves them all, by adding the move to the line.'));
      // where it is
      const where = h('div', { class: 'card' }, h('h3', {}, 'Where'));
      const at = t.position;
      const setAt = (k, v) => { const p = at.slice(); p[k] = v; this.place({ at: p }); };
      where.append(h('div', { class: 'xyz' }, numField('X', at[0], (v) => setAt(0, v)), numField('Y', at[1], (v) => setAt(1, v)), numField('Z', at[2], (v) => setAt(2, v))));
      const turnDeg = ((t.rotation[0] * 180 / Math.PI) % 360 + 360) % 360;
      where.append(h('div', { class: 'field', style: 'margin-top:8px' }, h('span', {}, 'Turn (°)'), h('div', { class: 'row' },
        h('button', { class: 'pill', onclick: () => this.place({ rotation: [t.rotation[0] - Math.PI / 12, t.rotation[1], t.rotation[2]] }) }, '−15°'),
        (() => { const i = h('input', { type: 'number', step: 5, value: S.num(turnDeg), inputmode: 'decimal', style: 'width:80px' }); i.onchange = () => this.place({ rotation: [+i.value * Math.PI / 180, t.rotation[1], t.rotation[2]] }); return i; })(),
        h('button', { class: 'pill', onclick: () => this.place({ rotation: [t.rotation[0] + Math.PI / 12, t.rotation[1], t.rotation[2]] }) }, '+15°'))));
      if (!sel.many) {
        where.append(h('div', { class: 'field' }, h('span', {}, 'Size'), h('div', { class: 'row' },
          h('button', { class: 'pill', onclick: () => this.place({ scale: t.scale.map((x) => Math.max(0.05, x / 1.25)) }) }, 'Smaller'),
          h('span', { class: 'dim mono', style: 'min-width:54px;text-align:center' }, '× ' + S.num(t.scale[0])),
          h('button', { class: 'pill', onclick: () => this.place({ scale: t.scale.map((x) => x * 1.25) }) }, 'Bigger'))));
      }
      body.append(where);
      // what it's made of
      const mat = h('div', { class: 'card' }, h('h3', {}, 'Made of'), h('div', { class: 'note', style: 'margin-top:0' }, t.material ? 'Now: ' + t.material : ''));
      const chips = h('div', { class: 'chips' });
      for (const m of MATERIALS) chips.append(h('button', { class: 'pill' + (t.material === m ? ' on' : ''), onclick: () => this.setMaterial('"' + m + '"', m) }, m));
      const sw = h('div', { class: 'chips', style: 'margin-top:10px' });
      for (const c of COLOURS) sw.append(h('button', { class: 'swatch', style: 'background:' + rgbCss(c), title: 'A plain colour', onclick: () => this.setMaterial('rgb(' + c.map(S.num).join(', ') + ')', 'a colour') }));
      mat.append(chips, sw);
      body.append(mat);
    },
    // a lamp or a sound picked in the view
    drawMark(mk) {
      body.append(h('div', { class: 'sh-head' }, h('h2', {}, mk.label), h('span', { class: 'sub' }, 'line ' + mk.line)));
      const line = S.lineAt(H.text, mk.line);
      const tools = h('div', { class: 'tools' },
        toolBtn('delete', 'Remove', async () => { const r = S.removeLine(H.text, mk.line); if (r.refused) return LE.toast('"' + r.name + '" is used on line ' + r.refused.join(', ') + ': change that first.'); V.selMark = null; await SP.apply(r.text, 'removed ' + mk.label); }, { danger: true }),
        toolBtn('code', 'Its line', () => { SP.show('logic'); LE.logic.gotoLine(mk.line); }),
        toolBtn('x', 'Let go', () => { V.selMark = null; this.draw(); }));
      body.append(tools);
      if (!mk.movable && mk.why) body.append(h('div', { class: 'note' }, mk.why));
      const card = h('div', { class: 'card' });
      if (mk.kind === 'lamp') {
        card.append(h('h3', {}, 'Light'));
        const lum = +(S.getArg(line, 'lumens') ?? 800), kel = +(S.getArg(line, 'kelvin') ?? 3000);
        card.append(slider('Brightness', lum, 10, 5000, 10, 'lm', (v) => this.setArgOn(mk.line, 'lumens', S.num(v), 'brightness')));
        card.append(slider('Warmth', kel, 1500, 9000, 100, 'K', (v) => this.setArgOn(mk.line, 'kelvin', S.num(v), 'warmth')));
      } else if (mk.kind === 'sound') {
        card.append(h('h3', {}, 'Sound'));
        const vol = +(S.getArg(line, 'volume') ?? 1);
        card.append(slider('Volume', vol, 0, 2, 0.05, '', (v) => this.setArgOn(mk.line, 'volume', S.num(v), 'volume')));
      }
      body.append(card);
    },
    async setArgOn(lineNo, key, value, what) {
      const old = S.lineAt(H.text, lineNo), line = S.setArg(old, key, value);
      if (line !== old) await SP.apply(S.replaceLine(H.text, lineNo, line), 'changed ' + what);
    },
    // the whole world: its title and how it starts
    worldCard() {
      const info = SP.info || {};
      const card = h('div', { class: 'card' }, h('h3', {}, 'This world'));
      const title = h('input', { type: 'text', value: info.title || '' });
      title.onchange = () => SP.apply(S.setStatement(H.text, 'title', JSON.stringify(title.value)), 'renamed the world');
      const mode = h('select', {}, h('option', { value: 'game' }, 'You walk around (game)'), h('option', { value: 'movie' }, 'It plays its shots (movie)'));
      mode.value = info.mode === 'movie' ? 'movie' : 'game';
      mode.onchange = () => SP.apply(S.setStatement(H.text, 'mode', JSON.stringify(mode.value)), 'changed how it starts');
      card.append(h('div', { class: 'field' }, h('span', {}, 'Title'), title), h('div', { class: 'field' }, h('span', {}, 'It starts'), mode));
      return card;
    },
    // every thing in the world, one row per line
    listCard() {
      const info = SP.info || SP.refreshInfo();
      const card = h('div', { class: 'card' }, h('h3', {}, 'In this world'));
      if (!info || !info.things.length) { card.append(h('div', { class: 'empty' }, 'Nothing yet: add a box above.')); return card; }
      const seen = new Map();
      info.things.forEach((t, i) => { if (t.line > 0 && !seen.has(t.line)) seen.set(t.line, { t, i, n: 0 }); if (seen.has(t.line)) seen.get(t.line).n++; });
      const list = h('div', { class: 'list' });
      let shown = 0;
      for (const [line, x] of seen) {
        if (++shown > 300) break;
        const ln = line - (E.lineOffset || 0);
        const lt = ln > 0 ? S.lineAt(H.text, ln) : '';
        const icon = ({ box: 'box', sphere: 'sphere', cylinder: 'cylinder', cone: 'cone', tree: 'tree', person: 'person', model: 'kind', ground: 'grid' })[S.callOf(lt)?.word] || 'part';
        list.append(h('button', { class: 'item', onclick: () => { V.select(x.i); V.frame(x.i); } }, I(icon), h('span', { class: 't' }, V.describe(x.t, lt) + (x.n > 1 ? ' ×' + x.n : '')), h('span', { class: 'r' }, ln > 0 ? 'line ' + ln : 'shared')));
      }
      card.append(list);
      return card;
    },
    // a move, turn or size typed in: the engine at once, the line rewritten
    place(change) {
      const sel = V.sel;
      if (!sel || sel.line <= 0) return;
      const t = E.thing(sel.index);
      const before = { at: t.position.slice(), rotation: t.rotation.slice(), scale: t.scale.slice() };
      const after = { at: change.at || before.at, rotation: change.rotation || before.rotation, scale: change.scale || before.scale };
      let line = S.lineAt(H.text, sel.line);
      const delta = [after.at[0] - before.at[0], after.at[1] - before.at[1], after.at[2] - before.at[2]];
      if (change.at) line = S.moveLine(line, after.at, delta, sel.many);
      if (change.rotation) line = S.turnLine(line, after.rotation[0], after.rotation[0] - before.rotation[0], sel.many);
      if (change.scale && !sel.many) line = S.scaleLine(line, after.scale);
      if (sel.many) {
        // a repeat: run the script again (each of its things moves by the same)
        SP.apply(S.replaceLine(H.text, sel.line, line), 'moved ' + sel.name);
        return;
      }
      E.thingSet(sel.index, change);
      H.commit(S.replaceLine(H.text, sel.line, line), (change.at ? 'moved ' : change.rotation ? 'turned ' : 'sized ') + sel.name, [{ index: sel.index, before, after }]);
      V.select(sel.index, true);
      this.draw();
    },
    async setMaterial(value, what) {
      const sel = V.sel;
      if (!sel || sel.line <= 0) return;
      const old = S.lineAt(H.text, sel.line);
      let line = S.getArg(old, 'made_of') !== null ? S.setArg(old, 'made_of', value) : S.setArg(old, 'material', value);
      if (line === old) return;
      await SP.apply(S.replaceLine(H.text, sel.line, line), 'made ' + sel.name + ' of ' + what);
    },
    async duplicate() {
      const sel = V.sel;
      if (!sel || sel.line <= 0) return;
      const t = E.thing(sel.index);
      const off = [0.5, 0, 0.5];
      const r = S.duplicateLine(H.text, sel.line, [t.position[0] + off[0], t.position[1], t.position[2] + off[2]], off, sel.many);
      if (!H.commit(r.text, 'copied ' + sel.name)) return;
      SP.pendingSelect = r.line;
      await SP.rerun();
    },
    async remove() {
      const sel = V.sel;
      if (!sel || sel.line <= 0) return;
      const r = S.removeLine(H.text, sel.line);
      if (r.refused) { LE.toast('"' + r.name + '" is used on line ' + r.refused.join(', ') + ': change that first, or make it smaller.'); return; }
      V.select(null);
      await SP.apply(r.text, 'removed ' + sel.name);
    },
  };
  function slider(label, value, min, max, step, unit, onchange) {
    const out = h('span', { class: 'dim mono', style: 'min-width:66px;text-align:right' }, S.num(value) + (unit ? ' ' + unit : ''));
    const inp = h('input', { type: 'range', min, max, step, value, style: 'flex:1;accent-color:var(--accent)' });
    inp.oninput = () => { out.textContent = S.num(+inp.value) + (unit ? ' ' + unit : ''); };
    inp.onchange = () => onchange(+inp.value);
    return h('div', { class: 'field' }, h('span', {}, label), h('div', { class: 'row' }, inp, out));
  }

  // ================================================================ Logic
  const KW = new Set(['after', 'and', 'break', 'by', 'continue', 'each', 'elif', 'else', 'emit', 'every', 'fn', 'for', 'forever', 'from', 'if', 'in', 'is', 'let', 'not', 'or', 'otherwise', 'repeat', 'return', 'task', 'times', 'to', 'wait', 'when', 'while', 'use']);
  const CONST = new Set(['true', 'false', 'yes', 'no', 'nothing', 'nil', 'pi', 'tau', 'infinity']);
  const WORDS = window.LOOP_WORDS || {};
  const WORLD = new Set(Object.keys(WORDS));
  const UNITS = 'm|cm|mm|km|s|ms|sec|min|minutes|h|hour|hours|deg|degrees|rad|K|lumens|lm|lux|nits|kg|g|Hz|kmh|mph|%';
  const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');
  function highlight(src, errLines, selLine) {
    const out = [];
    const re = new RegExp('(#.*$)|("(?:[^"\\\\]|\\\\.)*"?)|(\\b\\d+(?:\\.\\d+)?(?:\\s?(?:' + UNITS + ')\\b|%)?)|([A-Za-z_][A-Za-z0-9_]*)', 'gm');
    src.split('\n').forEach((line, i) => {
      let html = '', last = 0, m;
      re.lastIndex = 0;
      while ((m = re.exec(line))) {
        html += esc(line.slice(last, m.index));
        const t = esc(m[0]);
        if (m[1]) html += '<span class="tk-c">' + t + '</span>';
        else if (m[2]) html += '<span class="tk-s">' + t + '</span>';
        else if (m[3]) html += '<span class="tk-n">' + t + '</span>';
        else if (KW.has(m[4])) html += '<span class="tk-k">' + t + '</span>';
        else if (CONST.has(m[4])) html += '<span class="tk-b">' + t + '</span>';
        else if (WORLD.has(m[4])) html += '<span class="tk-w">' + t + '</span>';
        else html += t;
        last = m.index + m[0].length;
      }
      html += esc(line.slice(last));
      const cls = errLines.has(i + 1) ? 'errline' : selLine === i + 1 ? 'selline' : '';
      out.push(cls ? '<span class="' + cls + '">' + (html || ' ') + '</span>' : html);
    });
    return out.join('\n') + '\n';
  }
  const logic = {
    title: 'Logic', icon: 'code', problems: [], errLines: new Set(), typingT: 0, live: true, autocomplete: true, showMessages: false, messages: [],
    draw() {
      body.className = 'flush';
      body.innerHTML = '';
      this.gutter = h('div', { class: 'gutter', 'aria-hidden': 'true' });
      this.pre = h('pre', { 'aria-hidden': 'true' });
      this.ta = h('textarea', { spellcheck: 'false', autocapitalize: 'off', autocomplete: 'off', autocorrect: 'off', 'aria-label': 'The scene\'s script (Looped Script)' });
      this.area = h('div', { class: 'area' }, this.pre, this.ta);
      this.probBox = h('div', { class: 'problems' });
      this.sug = h('div', { class: 'suggest' });
      this.liveBtn = h('button', { class: 'pill' + (this.live ? ' on' : ''), title: 'Live: the world rebuilds a moment after you stop typing', onclick: () => LE.store.set('code.live', !this.live) }, 'Live');
      const bar = h('div', { class: 'codebar' },
        h('button', { class: 'pill primary', onclick: () => this.runNow(), title: 'Build the world from this script now (Ctrl+Enter)' }, I('play'), 'Run'),
        this.liveBtn,
        h('button', { class: 'pill', onclick: () => this.copy() }, I('copy'), 'Copy'),
        h('button', { class: 'pill', onclick: () => LE.download((E.scene || 'scene') + '.loop', H.text) }, 'Save .loop'),
        h('button', { class: 'pill' + (this.showMessages ? ' on' : ''), onclick: () => { this.showMessages = !this.showMessages; this.draw(); } }, 'Messages'),
        h('span', { class: 'dim', style: 'font-size:12px;margin-left:4px;white-space:nowrap' }, H.edited() ? 'changed from the file' : 'as the file'));
      if (this.showMessages) {
        const box = h('div', { class: 'messages', style: 'flex:1;overflow:auto;padding:10px 12px' });
        for (const m of this.messages.slice(-300)) box.append(h('div', { class: m.kind === 'error' ? 'e' : m.kind === 'warn' ? 'w' : m.kind === 'say' ? 's' : '' }, m.text));
        if (!this.messages.length) box.append(h('div', {}, 'The engine\'s messages show here: scenes starting, problems, what scripts say.'));
        body.append(bar, box);
        return;
      }
      body.append(bar, h('div', { class: 'code' }, this.gutter, this.area), this.sug, this.probBox);
      this.ta.value = H.text;
      this.ta.addEventListener('input', () => { this.paint(); this.typed(); this.suggest(); });
      this.ta.addEventListener('keyup', () => this.suggest());
      this.ta.addEventListener('click', () => { this.suggest(); this.caretSelect(); });
      this.ta.addEventListener('keydown', (e) => {
        if (e.key === 'Tab') { e.preventDefault(); document.execCommand('insertText', false, '  '); }
        if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); this.runNow(); }
      });
      this.area.addEventListener('scroll', () => { this.gutter.scrollTop = this.area.scrollTop; });
      this.gutter.addEventListener('click', (e) => {
        const r = this.gutter.getBoundingClientRect(), lh = parseFloat(getComputedStyle(this.pre).lineHeight) || 20;
        const n = Math.floor((e.clientY - r.top + this.gutter.scrollTop - 8) / lh) + 1;
        if (n >= 1) V.selectLine(n);
      });
      this.setProblems(this.problemText || '');
      this.paint();
      if (V.sel && V.sel.line > 0) setTimeout(() => this.gotoLine(V.sel.line, true), 0);
    },
    paint() {
      if (!this.ta || !this.ta.isConnected) return;
      const text = this.ta.value, n = text.split('\n').length, sel = V.sel && V.sel.line;
      let gl = '';
      for (let i = 1; i <= n; i++) gl += (this.errLines.has(i) ? '<span class="err">' + i + '</span>' : i === sel ? '<span class="hl">' + i + '</span>' : i) + '\n';
      this.gutter.innerHTML = gl;
      this.pre.innerHTML = highlight(text, this.errLines, sel);
      this.ta.style.height = '0px';
      this.ta.style.height = Math.max(this.area.clientHeight, this.ta.scrollHeight) + 'px';
      this.ta.style.width = '0px';
      this.ta.style.width = Math.max(this.area.clientWidth, this.pre.scrollWidth) + 'px';
    },
    // typing: kept at once, the world rebuilt when you pause (Live)
    typed() {
      clearTimeout(this.typingT);
      this.typingT = setTimeout(() => {
        if (H.commit(this.ta.value, 'typed in the script') && this.live) SP.rerun();
      }, 700);
    },
    runNow() {
      clearTimeout(this.typingT);
      if (this.ta && this.ta.isConnected) H.commit(this.ta.value, 'typed in the script');
      SP.rerun();
    },
    // the history changed under the script (undo, a move in the view): show it, keeping the caret
    sync() {
      if (!this.ta || !this.ta.isConnected || this.ta.value === H.text) return;
      const s = this.ta.selectionStart, top = this.area.scrollTop;
      this.ta.value = H.text;
      this.paint();
      try { this.ta.setSelectionRange(s, s); } catch (e) { /* not focused */ }
      this.area.scrollTop = top;
    },
    gotoLine(n, noFocus) {
      if (!this.ta || !this.ta.isConnected) return;
      const L = this.ta.value.split('\n');
      let pos = 0;
      for (let i = 0; i < n - 1 && i < L.length; i++) pos += L[i].length + 1;
      const lh = parseFloat(getComputedStyle(this.pre).lineHeight) || 20;
      this.area.scrollTop = Math.max(0, (n - 4) * lh);
      if (!noFocus) { this.ta.focus({ preventScroll: true }); this.ta.setSelectionRange(pos, pos + (L[n - 1] || '').length); }
      this.paint();
    },
    // the caret's line picks its thing in the world
    caretSelect() {
      const n = this.ta.value.slice(0, this.ta.selectionStart).split('\n').length;
      if (!V.sel || V.sel.line !== n) V.selectLine(n);
    },
    setProblems(t) {
      this.problemText = t;
      this.problems = t ? t.trim().split('\n').filter(Boolean) : [];
      this.errLines = new Set();
      if (!this.probBox) return;
      this.probBox.innerHTML = '';
      for (const p of this.problems) {
        const m = p.match(/line (\d+)/);
        const n = m ? +m[1] - (E.lineOffset || 0) : 0;
        const text = m ? (n > 0 ? p.replace(/scene\.loop line \d+/, (E.scene || 'scene') + '.loop line ' + n) : p.replace(/scene\.loop line \d+/, 'a shared script (use "…")')) : p;
        if (n > 0) this.errLines.add(n);
        const d = h('div', {}, '⚠ ' + text);
        if (n > 0) d.onclick = () => this.gotoLine(n);
        this.probBox.append(d);
      }
      this.paint();
    },
    // words of the language that fit what's being typed, one tap to finish it
    suggest() {
      if (!this.sug) return;
      const ta = this.ta, before = ta.value.slice(0, ta.selectionStart);
      const m = before.match(/[A-Za-z_][A-Za-z0-9_]*$/);
      this.sug.innerHTML = '';
      if (!m || m[0].length < 2 || this.autocomplete === false) return;
      const w = m[0].toLowerCase();
      const hits = Object.keys(WORDS).filter((k) => k.startsWith(w) && k !== w).slice(0, 12);
      for (const k of hits) {
        this.sug.append(h('button', { title: WORDS[k], onmousedown: (e) => e.preventDefault(), onclick: () => {
          ta.focus();
          ta.setSelectionRange(ta.selectionStart - m[0].length, ta.selectionStart);
          document.execCommand('insertText', false, k);
          this.suggest();
        } }, k));
      }
    },
    async copy() {
      try { await navigator.clipboard.writeText(H.text); LE.toast('Copied ' + (E.scene || 'the scene') + '.loop: paste it in the chat to keep it in the repo'); }
      catch (e) { prompt('Copy the script:', H.text); }
    },
  };
  LE.logic = logic;

  // ================================================================ Film
  const LOOKS = ['natural', 'blockbuster', 'noir', 'golden', 'bleach', 'seventies', 'neon', 'dream', 'documentary', 'horror', 'arctic', 'western'];
  const film = {
    title: 'Film', icon: 'film', selShot: -1, playing: false,
    draw() {
      body.className = '';
      body.innerHTML = '';
      const info = SP.info || SP.refreshInfo() || { shotList: [] };
      const shots = info.shotList || [];
      const total = shots.reduce((t, s) => t + s.duration, 0);
      body.append(h('div', { class: 'sh-head' }, h('h2', {}, 'Film'), h('span', { class: 'sub' }, shots.length ? shots.length + ' shot' + (shots.length > 1 ? 's' : '') + ' · ' + S.num(total) + ' s' : 'No shots yet')));
      const lines = S.shotLines(H.text);
      const linked = lines.length === shots.length;
      const tools = h('div', { class: 'tools' },
        toolBtn(this.playing ? 'pause' : 'play', this.playing ? 'Pause' : 'Watch', () => this.togglePlay(), { disabled: !shots.length, on: this.playing }),
        toolBtn('camera', 'Shot from here', () => this.addShot()),
        toolBtn('turn', 'Retake', () => this.retake('from'), { disabled: this.selShot < 0 || !linked, title: 'This shot starts where the view is now' }),
        toolBtn('arrow', 'Ends here', () => this.retake('to'), { disabled: this.selShot < 0 || !linked, title: 'The camera moves to where the view is now by the end of this shot' }),
        toolBtn('delete', 'Remove', () => this.removeShot(), { danger: true, disabled: this.selShot < 0 || !linked }));
      body.append(tools);
      // the timeline
      const tl = h('div', { class: 'timeline' });
      if (shots.length) {
        let t0 = 0;
        shots.forEach((s, i) => {
          const blk = h('div', { class: 'blk' + (i === this.selShot ? ' on' : ''), style: 'left:calc(' + (t0 / total * 100) + '% + 2px);width:calc(' + (s.duration / total * 100) + '% - 4px)' }, s.name, h('small', {}, S.num(s.duration) + ' s'));
          tl.append(blk);
          t0 += s.duration;
        });
        const ruler = h('div', { class: 'ruler' });
        const step = total > 60 ? 10 : total > 20 ? 5 : total > 8 ? 2 : 1;
        for (let s = 0; s <= total + 1e-6; s += step) ruler.append(h('span', { style: 'left:' + (s / total * 100) + '%' }, s + 's'));
        tl.append(ruler);
        this.head = h('div', { class: 'head', style: 'left:0' });
        tl.append(this.head);
        // tap a shot to pick it; drag to scrub
        const at = (e) => { const r = tl.getBoundingClientRect(); return Math.max(0, Math.min(total, (e.clientX - r.left) / r.width * total)); };
        tl.onpointerdown = (e) => {
          tl.setPointerCapture(e.pointerId);
          const t = at(e);
          let acc = 0;
          this.selShot = shots.findIndex((s) => (acc += s.duration) >= t);
          this.scrub(t);
          tl.onpointermove = (ev) => this.scrub(at(ev));
        };
        tl.onpointerup = () => { tl.onpointermove = null; this.draw(); };
        this.total = total;
      } else tl.append(h('div', { class: 'empty', style: 'padding:24px 10px;font-size:13px' }, 'Frame the view you want with your fingers, then tap Shot from here.'));
      body.append(tl);
      if (shots.length && !linked) body.append(h('div', { class: 'note' }, 'Some shots are made by a repeat or a shared script, so Retake and Remove are off: change them in Logic.'));
      // the selected shot
      if (this.selShot >= 0 && shots[this.selShot] && linked) {
        const s = shots[this.selShot], lineNo = lines[this.selShot];
        const card = h('div', { class: 'card' }, h('h3', {}, s.name + ' · line ' + lineNo));
        card.append(h('div', { class: 'field' }, h('span', {}, 'Length'), h('div', { class: 'row' },
          h('button', { class: 'pill', onclick: () => this.setLength(lineNo, Math.max(0.5, s.duration - 0.5)) }, '−½ s'),
          h('span', { class: 'dim mono', style: 'min-width:50px;text-align:center' }, S.num(s.duration) + ' s'),
          h('button', { class: 'pill', onclick: () => this.setLength(lineNo, s.duration + 0.5) }, '+½ s'))));
        body.append(card);
      }
      // the look of the film
      const lookNow = (H.text.match(/^look\s+"([^"]+)"/m) || [])[1] || 'natural';
      const looks = h('div', { class: 'card' }, h('h3', {}, 'The look'));
      const chips = h('div', { class: 'chips' });
      for (const l of LOOKS) chips.append(h('button', { class: 'pill' + (l === lookNow ? ' on' : ''), onclick: () => SP.apply(S.setStatement(H.text, 'look', '"' + l + '"'), 'the ' + l + ' look') }, l));
      looks.append(chips, h('div', { class: 'note' }, 'What you watch here is the film itself: the same camera and the same renderer as the final picture.'));
      body.append(looks);
    },
    tick() {
      if (!this.head || !this.head.isConnected || !this.total) return;
      const st = E.stats();
      if (V.mode === 'film') this.head.style.left = (Math.min(st.film || 0, this.total) / this.total * 100) + '%';
    },
    togglePlay() {
      this.playing = !this.playing;
      if (this.playing) {
        V.setMode('film');
        E.setOption('pause', 0);
      } else E.setOption('pause', 1);
      this.draw();
    },
    scrub(t) {
      V.setMode('film');
      E.setOption('filmtime', t);
      if (!this.playing) { E.setOption('pause', 1); E.setOption('step', 1); }
      if (this.head) this.head.style.left = (t / this.total * 100) + '%';
    },
    shotText(name, dur) {
      const v = E.view();
      const at = v.at, look = v.look_at, lens = Math.round(v.lens || V.cam.lens);
      return 'shot ' + JSON.stringify(name) + ', ' + S.num(dur) + ' s, from: {at: ' + S.vec(at) + ', look_at: ' + S.vec(look) + ', lens: ' + lens + ' mm}';
    },
    async addShot() {
      if (V.mode !== 'edit') V.setMode('edit');
      const lines = S.shotLines(H.text);
      const name = 'Shot ' + (lines.length + 1);
      let text;
      if (lines.length) text = S.insertAfter(H.text, lines[lines.length - 1], this.shotText(name, 3));
      else { const L = H.text.split('\n'); let end = L.length; while (end > 0 && L[end - 1].trim() === '') end--; text = S.insertAfter(H.text, end, ['', '# ---- the film', this.shotText(name, 3)]); }
      this.selShot = lines.length;
      await SP.apply(text, 'added ' + name.toLowerCase());
      LE.toast(name + ' added: it starts where the view is now');
    },
    async retake(which) {
      const lines = S.shotLines(H.text), n = lines[this.selShot];
      if (!n) return;
      const old = S.lineAt(H.text, n), v = E.view();
      const val = '{at: ' + S.vec(v.at) + ', look_at: ' + S.vec(v.look_at) + ', lens: ' + Math.round(v.lens || V.cam.lens) + ' mm}';
      await SP.apply(S.replaceLine(H.text, n, S.setArg(old, which, val)), which === 'from' ? 'retook a shot' : 'a shot\'s end');
    },
    async setLength(lineNo, d) {
      const old = S.lineAt(H.text, lineNo);
      const line = old.replace(/^(\s*(?:shot|orbit)\s+"[^"]*"\s*,\s*)[\d.]+\s*(s|sec|seconds)?/, '$1' + S.num(d) + ' s');
      if (line !== old) await SP.apply(S.replaceLine(H.text, lineNo, line), 'a shot\'s length');
    },
    async removeShot() {
      const lines = S.shotLines(H.text), n = lines[this.selShot];
      if (!n) return;
      const r = S.removeLine(H.text, n);
      this.selShot = -1;
      await SP.apply(r.text, 'removed a shot');
    },
  };

  // ================================================================ Sound
  const ENDLESS = ['fire', 'wind', 'rain', 'stream', 'birds', 'crickets', 'night', 'hum', 'crowd', 'applause', 'strings', 'synth', 'drones', 'heartbeat', 'chains'];
  const sound = {
    title: 'Sound', icon: 'wave',
    draw() {
      body.className = '';
      body.innerHTML = '';
      const on = LE.soundOn && LE.soundOn();
      body.append(h('div', { class: 'sh-head' }, h('h2', {}, 'Sound'), h('span', { class: 'spacer' }),
        h('button', { class: 'pill' + (on ? ' on' : ''), onclick: () => { LE.toggleSound(); this.draw(); } }, I(on ? 'sound' : 'speaker'), on ? 'Hearing it' : 'Listen')));
      const list = S.soundLines(H.text);
      const add = h('div', { class: 'tools' });
      for (const k of ENDLESS) add.append(h('button', { class: 'tool', onclick: () => addLine('sound "' + k + '", at: ' + S.vec(spotAhead(1)) + ', volume: 1', 'added the sound of ' + k) }, I('wave'), k));
      body.append(this.musicCard());
      body.append(h('h3', { style: 'margin:14px 0 4px;font-size:14px;color:var(--dim);font-weight:600' }, 'Sounds of the world'), h('div', { class: 'note', style: 'margin-top:0' }, 'Tap a sound to place it in the middle of the view. You hear it from where it is, louder as you get closer.'), add);
      const card = h('div', { class: 'card' }, h('h3', {}, 'Sounds in this world'));
      if (!list.length) card.append(h('div', { class: 'empty' }, 'Silence so far.'));
      for (const s of list) {
        const row = h('div', { class: 'field' }, h('span', {}, s.kind || 'sound'), h('div', { class: 'row' },
          (() => { const i = h('input', { type: 'range', min: 0, max: 2, step: 0.05, value: s.volume, style: 'flex:1;accent-color:var(--accent)' }); i.onchange = () => build.setArgOn(s.line, 'volume', S.num(+i.value), 'volume'); return i; })(),
          s.at ? h('button', { class: 'pill', onclick: () => { V.cam.target = s.at.slice(); V.cam.dist = Math.max(3, V.cam.dist * 0.6); V.pushCamera(); } }, 'Show') : h('span', { class: 'dim', style: 'font-size:12px' }, 'everywhere')));
        card.append(row);
      }
      body.append(card);
      // the room's echo
      const roomLine = (H.text.match(/^room\b.*$/m) || [''])[0];
      const echo = +(S.getArg(roomLine, 'echo') ?? 0.15), size = +(S.getArg(roomLine, 'size') ?? 0.3);
      const room = h('div', { class: 'card' }, h('h3', {}, 'The room'), h('div', { class: 'note', style: 'margin-top:0' }, 'How big the space sounds: 0 outdoors, 1 a cathedral.'));
      room.append(slider('Size', size, 0, 1, 0.05, '', (v) => SP.apply(S.setStatement(H.text, 'room', 'size: ' + S.num(v) + ', echo: ' + S.num(echo)), 'the room\'s size')));
      room.append(slider('Echo', echo, 0, 1, 0.05, '', (v) => SP.apply(S.setStatement(H.text, 'room', 'size: ' + S.num(size) + ', echo: ' + S.num(v)), 'the room\'s echo')));
      body.append(room);
    },
    // ---- music: a drum grid and a melody grid, each row a `track` line
    music() {
      const out = { tempo: 100, swing: 0, tracks: [] };
      H.text.split('\n').forEach((l, i) => {
        const c = S.callOf(l);
        if (!c || /^\s/.test(l)) return;
        if (c.word === 'tempo') { out.tempo = +((l.slice(c.argsAt).match(/^\s*([\d.]+)/) || [])[1] || 100); out.swing = +(S.getArg(l, 'swing') ?? 0); out.tempoLine = i + 1; }
        if (c.word === 'track') {
          const m = l.slice(c.argsAt).match(/^\s*"([^"]+)"\s*,\s*"([^"]*)"/);
          if (m) out.tracks.push({ line: i + 1, instrument: m[1], pattern: m[2], volume: +(S.getArg(l, 'volume') ?? 1) });
        }
      });
      return out;
    },
    // a changed song: written into the script, and played at once without building the world again
    async setMusic(text, label) {
      if (!H.commit(text, label)) return;
      const m = this.music();
      if (!E.musicSet({ tempo: m.tempo, swing: m.swing, tracks: m.tracks.map((t) => ({ instrument: t.instrument, pattern: t.pattern, volume: t.volume })) })) await SP.rerun();
      SP.marks();
      this.draw();
    },
    // where music lines go: after the last one, else at the end under a heading
    musicText(line) {
      const m = this.music(), lines = [m.tempoLine || 0, ...m.tracks.map((t) => t.line)];
      const last = Math.max(...lines);
      if (last > 0) return S.insertAfter(H.text, last, line);
      const L = H.text.split('\n');
      let end = L.length;
      while (end > 0 && L[end - 1].trim() === '') end--;
      return S.insertAfter(H.text, end, ['', '# ---- the music (16 steps a bar: 4 steps a beat)', 'tempo 100', line]);
    },
    setTrack(instrument, steps, isDrum) {
      const m = this.music();
      const t = m.tracks.find((x) => x.instrument === instrument);
      const pattern = isDrum ? steps.map((x) => (x ? 'x' : '.')).join('') : steps.map((x) => x || '.').join(' ');
      const empty = steps.every((x) => !x);
      if (t && empty) return this.setMusic(S.removeLine(H.text, t.line).text, 'cleared the ' + instrument);
      if (t) {
        const old = S.lineAt(H.text, t.line), line = old.replace(/^(track\s+"[^"]+"\s*,\s*)"[^"]*"/, '$1"' + pattern + '"');
        return this.setMusic(S.replaceLine(H.text, t.line, line), 'the ' + instrument);
      }
      return this.setMusic(this.musicText('track "' + instrument + '", "' + pattern + '"'), 'added the ' + instrument);
    },
    musicCard() {
      const m = this.music(), on = LE.soundOn && LE.soundOn();
      const card = h('div', { class: 'card' });
      card.append(h('div', { class: 'row', style: 'margin-bottom:6px' }, h('h3', { style: 'margin:0' }, 'Music'), h('span', { class: 'spacer' }),
        h('span', { class: 'dim', style: 'font-size:12px' }, on ? 'playing with the world' : 'tap Listen to hear it')));
      card.append(slider('Tempo', m.tempo, 60, 180, 1, 'bpm', (v) => {
        const line = 'tempo ' + S.num(v) + (m.swing ? ', swing: ' + S.num(m.swing) : '');
        this.setMusic(m.tempoLine ? S.replaceLine(H.text, m.tempoLine, line) : this.musicText(line), 'the tempo');
      }));
      card.append(slider('Swing', m.swing, 0, 0.5, 0.05, '', (v) => {
        const line = 'tempo ' + S.num(m.tempo) + (v ? ', swing: ' + S.num(v) : '');
        this.setMusic(m.tempoLine ? S.replaceLine(H.text, m.tempoLine, line) : this.musicText(line), 'the swing');
      }));
      // drums: one row a drum, one cell a step
      const DRUMS = ['kick', 'snare', 'hat', 'open_hat', 'clap', 'tom'];
      const drumGrid = h('div', { class: 'seq' });
      for (const d of DRUMS.slice(0, 5)) {
        const t = m.tracks.find((x) => x.instrument === d);
        const steps = Array.from({ length: 16 }, (_, i) => !!(t && /[xX]/.test(t.pattern.replace(/\s+/g, '')[i] || '')));
        const row = h('div', { class: 'seqrow' }, h('span', { class: 'seqlbl', title: d.replace('_', ' ') }, d === 'open_hat' ? 'open' : d));
        steps.forEach((x, i) => row.append(h('button', { class: 'cell' + (x ? ' on' : '') + (i % 4 === 0 ? ' beat' : ''), 'data-step': i, 'aria-label': d + ' step ' + (i + 1), onclick: () => { steps[i] = !steps[i]; this.setTrack(d, steps, true); } })));
        drumGrid.append(row);
      }
      card.append(h('div', { class: 'note', style: 'margin:8px 0 4px' }, 'Drums: tap a step to hit it. Each row loops every bar.'), drumGrid);
      // a melody: pick an instrument, then one note per step from a scale that always sounds right
      const MELODIC = ['bass', 'keys', 'pluck', 'lead', 'pad', 'bell'];
      const OCT = { bass: 2, pluck: 3, keys: 4, pad: 4, lead: 5, bell: 5 };
      this.melodic = this.melodic || (m.tracks.find((t) => MELODIC.includes(t.instrument)) || {}).instrument || 'bass';
      const ins = this.melodic, o = OCT[ins];
      const chips = h('div', { class: 'chips', style: 'margin:12px 0 6px' });
      for (const k of MELODIC) chips.append(h('button', { class: 'pill' + (k === ins ? ' on' : '') + (m.tracks.some((t) => t.instrument === k) ? ' has' : ''), onclick: () => { this.melodic = k; this.draw(); } }, k));
      card.append(chips);
      const scale = ['C' + o, 'Eb' + o, 'F' + o, 'G' + o, 'Bb' + o, 'C' + (o + 1), 'Eb' + (o + 1), 'F' + (o + 1)].reverse();
      const t = m.tracks.find((x) => x.instrument === ins);
      const words = t ? t.pattern.trim().split(/\s+/) : [];
      const steps = Array.from({ length: 16 }, (_, i) => (words[i] && words[i] !== '.' && words[i] !== '-' ? words[i] : null));
      const mel = h('div', { class: 'seq' });
      for (const n of scale) {
        const row = h('div', { class: 'seqrow' }, h('span', { class: 'seqlbl mono' }, n));
        steps.forEach((x, i) => row.append(h('button', { class: 'cell' + (x === n ? ' on note' : '') + (i % 4 === 0 ? ' beat' : ''), 'data-step': i, 'aria-label': n + ' step ' + (i + 1), onclick: () => { steps[i] = steps[i] === n ? null : n; this.setTrack(ins, steps, false); } })));
        mel.append(row);
      }
      card.append(h('div', { class: 'note', style: 'margin:4px 0' }, 'Melody: one note a step from a scale that always sounds right; tap it again for silence.'), mel);
      this.grids = [drumGrid, mel];
      return card;
    },
    tick() {
      if (!this.grids || !this.grids[0].isConnected) return;
      const st = E.musicStep();
      const col = st < 0 ? -1 : st % 16;
      if (col === this.lastCol) return;
      this.lastCol = col;
      for (const g of this.grids) g.querySelectorAll('.cell').forEach((c) => c.classList.toggle('now', +c.dataset.step === col));
    },
  };

  // Build's copy and remove, for the keys (Ctrl+D, Delete)
  SP.buildApi = {
    duplicate: () => build.duplicate(),
    removeAny: async () => {
      const mk = V.selMark;
      if (!mk) return build.remove();
      const r = S.removeLine(H.text, mk.line);
      if (r.refused) return LE.toast('"' + r.name + '" is used on line ' + r.refused.join(', ') + ': change that first.');
      V.selMark = null;
      await SP.apply(r.text, 'removed ' + mk.label);
    },
  };

  // ================================================================ switching
  const ALL = { build, logic, film, sound };
  const tabs = [...document.querySelectorAll('#spaces button')];
  tabs.forEach((b) => { b.prepend(I(ALL[b.dataset.space].icon)); b.onclick = () => SP.show(b.dataset.space, true); });
  SP.show = (name, fromTab) => {
    const sheet = document.getElementById('sheet');
    if (fromTab && SP.current === name && !sheet.classList.contains('closed')) { sheet.classList.add('closed'); document.body.classList.add('sheet-closed'); tabs.forEach((b) => b.classList.remove('on')); SP.current = null; return; }
    sheet.classList.remove('closed');
    document.body.classList.remove('sheet-closed');
    if (name !== 'film' && V.mode === 'film') { film.playing = false; V.setMode('edit'); }
    if (name !== 'build' && LE.studio && LE.studio.open) LE.studio.leave(true);  // another space: the Studio's thing goes back to the world
    SP.current = name;
    tabs.forEach((b) => b.classList.toggle('on', b.dataset.space === name));
    SP.marks();
    ALL[name].draw();
    try { localStorage.setItem('loop3d.editor.space', name); } catch (e) { /* private mode */ }
  };
  SP.redraw = () => { if (SP.current) ALL[SP.current].draw(); };
  // lamps and sounds shown in the world, from their lines
  SP.marks = () => {
    const out = [];
    H.text.split('\n').forEach((l, i) => {
      const c = S.callOf(l);
      if (!c || /^\s/.test(l) && c.word !== 'sound' && c.word !== 'light') return;
      if (c.word === 'light' || c.word === 'panel_light') {
        const at = S.vecLiteral(S.getArg(l, 'at'));
        const pos = at || S.vecLiteral((l.slice(c.argsAt).match(/^\s*(vec\([^()]*\))/) || [])[1]);
        if (pos) out.push({ at: pos, glyph: '☀', label: c.name || 'lamp', color: '#ffffff', line: i + 1, kind: 'lamp', movable: !!at, why: at ? '' : 'its place is written first in its line: change it in Logic' });
      } else if (c.word === 'sound') {
        const at = S.vecLiteral(S.getArg(l, 'at'));
        const kind = (l.slice(c.argsAt).match(/^\s*"([^"]+)"/) || [])[1] || 'sound';
        if (at) out.push({ at, glyph: '♪', label: kind, color: '#a8a8a8', line: i + 1, kind: 'sound', movable: true });
      }
    });
    if (V.selMark) V.selMark = out.find((m) => m.line === V.selMark.line) || null;
    V.extra = SP.current === 'film' || !LE.store.get('render.showLights') ? [] : out;
  };

  V.on((kind) => {
    if (kind === 'select' || kind === 'mark') { if (SP.current === 'build') build.draw(); else if (SP.current === 'logic') logic.paint(); }
    if (kind === 'changed') { logic.sync(); SP.marks(); if (SP.current === 'build') build.draw(); }
    if (kind === 'filmLeft') { film.playing = false; if (SP.current === 'film') film.draw(); }
  });
  V.on((kind, d) => { if (kind === 'changed' && d && d.rerun) SP.rerun(); });
  setInterval(() => { if (SP.current === 'film') film.tick(); else if (SP.current === 'sound') sound.tick(); }, 60);
})();
