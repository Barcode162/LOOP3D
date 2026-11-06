// The editor's building blocks: the settings store, property rows, menus,
// the command palette, dialogs and toasts.
window.LE = window.LE || {};
(() => {
  const $ = (sel, root = document) => root.querySelector(sel);
  const h = (tag, attrs = {}, ...kids) => {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v === undefined || v === null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'style') el.style.cssText = v;
      else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
      else if (k === 'html') el.innerHTML = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
    for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false) el.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
    return el;
  };
  LE.$ = $; LE.h = h;
  const safe = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* private mode */ } },
  };
  LE.safe = safe;

  // ------------------------------------------------------------ settings store
  const items = new Map();   // id -> item (with .scope and .cat)
  for (const scope of ['project', 'editor'])
    for (const cat of LE.SCHEMA[scope]) for (const g of cat.groups) for (const it of g.items) { it.scope = scope; it.cat = cat; it.group = g; items.set(it.id, it); }
  const values = { project: {}, editor: {} };
  const listeners = [];
  const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const store = {
    items, values,
    project: 'default',
    key(scope) { return scope === 'editor' ? 'loop3d.editor.prefs' : 'loop3d.project.' + store.project + '.settings'; },
    load(scope) {
      try { values[scope] = JSON.parse(safe.get(store.key(scope)) || '{}') || {}; } catch (e) { values[scope] = {}; }
    },
    save(scope) { safe.set(store.key(scope), JSON.stringify(values[scope])); },
    get(id) { const it = items.get(id); if (!it) return undefined; const v = values[it.scope][id]; return v === undefined ? it.def : v; },
    isModified(id) { const it = items.get(id); return it && values[it.scope][id] !== undefined && !eq(values[it.scope][id], it.def); },
    set(id, v, quiet) {
      const it = items.get(id);
      if (!it) return;
      if (eq(v, it.def)) delete values[it.scope][id]; else values[it.scope][id] = v;
      store.save(it.scope);
      if (!quiet) listeners.forEach((fn) => fn(id, v, it));
    },
    reset(id) { const it = items.get(id); if (it) store.set(id, it.def); },
    resetMany(ids) { ids.forEach((id) => store.reset(id)); },
    on(fn) { listeners.push(fn); },
    modifiedIn(scope) { return Object.keys(values[scope]).filter((id) => items.has(id)); },
    // live: reaches the engine (or the editor) as you change it; build: read by
    // the site build from project.json; nb: a feature not built yet; you: needs you
    status(it) {
      if (it.bind === 'build') return 'build';
      if (it.bind) return 'live';
      return it.status === 'you' ? 'you' : 'nb';
    },
    exportJson(scope) { return JSON.stringify({ loop3d: scope === 'editor' ? 'editor-preferences' : 'project-settings', project: scope === 'project' ? store.project : undefined, values: values[scope] }, null, 2); },
    importJson(scope, text) {
      const o = JSON.parse(text);
      const vals = o.values || o;
      let n = 0;
      for (const [id, v] of Object.entries(vals)) if (items.has(id) && items.get(id).scope === scope) { store.set(id, v); n++; }
      return n;
    },
  };
  LE.store = store;

  // ------------------------------------------------------------ property rows
  const fmtVal = (it, v) => {
    if (it.fmt) return it.fmt(v);
    if (typeof v === 'number') {
      const d = it.step && it.step < 1 ? Math.min(4, String(it.step).split('.')[1]?.length || 2) : 0;
      return (+v).toFixed(d) + (it.unit ? ' ' + it.unit : '');
    }
    return String(v);
  };
  function badgeFor(it) {
    const st = store.status(it);
    if (st === 'nb') return h('span', { class: 'badge nw', title: (it.help ? it.help + ' ' : '') + 'A feature the engine doesn\'t have yet: on the roadmap (docs/TUNING.md).' }, 'Not built yet');
    if (st === 'you') return h('span', { class: 'badge rs', title: (it.help ? it.help + ' ' : '') + 'This one needs you.' }, 'Needs you');
    if (st === 'build') return h('span', { class: 'badge', title: 'Read by the site build (tools/build_site.sh) from project.json: use File → Save to project.' }, 'Build');
    return null;
  }
  // A row for one setting. get/set default to the store.
  function propRow(it, opt = {}) {
    const get = opt.get || (() => store.get(it.id));
    const set = opt.set || ((v) => store.set(it.id, v));
    const isMod = opt.isModified || (() => store.isModified(it.id));
    const row = h('div', { class: 'prop', 'data-id': it.id });
    const lbl = h('div', { class: 'lbl', title: (it.help ? it.help + '\n' : '') + it.id }, h('i', { class: 'mod' }), it.label);
    const ctl = h('div', { class: 'ctl' });
    const side = h('div', { class: 'side' });
    const badge = opt.noBadge ? null : badgeFor(it);
    if (badge) side.append(badge);
    const resetBtn = h('button', { class: 'reset', title: 'Reset to default (' + fmtVal(it, it.def) + ')', onclick: () => { set(it.def); refresh(); } }, '↺');
    side.append(resetBtn);
    let refresh = () => {};
    const v = get();
    const commit = (val) => { set(val); row.classList.toggle('modified', isMod()); };
    switch (it.type) {
      case 'toggle': {
        const inp = h('input', { type: 'checkbox' });
        inp.checked = !!v;
        if (it.always) inp.disabled = true;
        inp.onchange = () => commit(inp.checked);
        ctl.append(h('label', { class: 'switch' }, inp, h('i')));
        refresh = () => { inp.checked = !!get(); row.classList.toggle('modified', isMod()); };
        break;
      }
      case 'slider': {
        const inp = h('input', { type: 'range', min: it.min, max: it.max, step: it.step || 'any' });
        const out = h('span', { class: 'val' });
        inp.value = v; out.textContent = fmtVal(it, +v);
        inp.oninput = () => { out.textContent = fmtVal(it, +inp.value); commit(+inp.value); };
        out.ondblclick = () => {
          const t = prompt(it.label, get());
          if (t !== null && t !== '' && !isNaN(+t)) { commit(+t); refresh(); }
        };
        out.title = 'Double-click to type a value';
        ctl.append(inp, out);
        refresh = () => { const x = get(); inp.value = x; out.textContent = fmtVal(it, +x); row.classList.toggle('modified', isMod()); };
        break;
      }
      case 'number': {
        const inp = h('input', { type: 'number', min: it.min, max: it.max, step: it.step || 'any' });
        inp.value = v;
        inp.onchange = () => { if (inp.value !== '') commit(+inp.value); };
        ctl.append(inp);
        if (it.fmt || it.unit) { const out = h('span', { class: 'val' }, fmtVal(it, +v)); ctl.append(out); inp.addEventListener('input', () => { out.textContent = fmtVal(it, +inp.value); }); refresh = () => { inp.value = get(); out.textContent = fmtVal(it, +get()); row.classList.toggle('modified', isMod()); }; }
        else refresh = () => { inp.value = get(); row.classList.toggle('modified', isMod()); };
        break;
      }
      case 'enum': {
        const sel = h('select');
        for (const o of it.options) sel.append(h('option', { value: JSON.stringify(o.value) }, o.label));
        sel.value = JSON.stringify(v);
        sel.onchange = () => commit(JSON.parse(sel.value));
        ctl.append(sel);
        refresh = () => { sel.value = JSON.stringify(get()); row.classList.toggle('modified', isMod()); };
        break;
      }
      case 'color': {
        const inp = h('input', { type: 'color' });
        const out = h('span', { class: 'val' });
        inp.value = v; out.textContent = v;
        inp.oninput = () => { out.textContent = inp.value; commit(inp.value); };
        ctl.append(inp, out);
        refresh = () => { inp.value = get(); out.textContent = get(); row.classList.toggle('modified', isMod()); };
        break;
      }
      case 'vec3': {
        const wrap = h('div', { class: 'vec' });
        const ins = [0, 1, 2].map((i) => {
          const inp = h('input', { type: 'number', step: it.step || 0.01, min: it.min, max: it.max, title: 'XYZ'[i] + ' / ' + 'RGB'[i] });
          inp.value = v[i];
          inp.onchange = () => { const cur = [...get()]; cur[i] = +inp.value; commit(cur); };
          wrap.append(inp);
          return inp;
        });
        ctl.append(wrap);
        refresh = () => { const cur = get(); ins.forEach((inp, i) => { inp.value = cur[i]; }); row.classList.toggle('modified', isMod()); };
        break;
      }
      case 'key': {
        const btn = h('button', { class: 'keycap', title: 'Click, then press a key' }, v);
        btn.onclick = () => {
          btn.textContent = 'Press a key…';
          const onKey = (e) => { e.preventDefault(); e.stopPropagation(); removeEventListener('keydown', onKey, true); commit(e.code); btn.textContent = e.code; };
          addEventListener('keydown', onKey, true);
        };
        ctl.append(btn);
        refresh = () => { btn.textContent = get(); row.classList.toggle('modified', isMod()); };
        break;
      }
      default: {
        const inp = h('input', { type: 'text' });
        inp.value = v;
        inp.onchange = () => commit(inp.value);
        ctl.append(inp);
        refresh = () => { inp.value = get(); row.classList.toggle('modified', isMod()); };
      }
    }
    row.append(lbl, ctl, side);
    if (it.help && opt.showHelp) row.append(h('div', { class: 'help' }, it.help));
    row.classList.toggle('modified', isMod());
    row.refresh = refresh;
    return row;
  }
  LE.propRow = propRow;

  // A collapsible group of rows.
  function propGroup(title, rows, opt = {}) {
    const d = h('details', { class: 'pgroup', open: opt.open !== false });
    const sum = h('summary', {}, title);
    if (opt.action) sum.append(h('span', { class: 'act' }, opt.action));
    d.append(sum, ...rows);
    return d;
  }
  LE.propGroup = propGroup;

  // ------------------------------------------------------------ toasts
  LE.toast = (text, kind) => {
    const t = h('div', { class: 'toast' + (kind === 'err' ? ' err' : '') }, text);
    $('#toastRoot').append(t);
    setTimeout(() => { t.style.transition = 'opacity .4s'; t.style.opacity = '0'; setTimeout(() => t.remove(), 450); }, 2600);
  };

  // ------------------------------------------------------------ dialogs
  LE.modal = (opt) => {
    const back = h('div', { class: 'modal-back' });
    const box = h('div', { class: 'modal ' + (opt.cls || ''), role: 'dialog', 'aria-label': opt.title || '' });
    const close = () => { back.remove(); removeEventListener('keydown', onKey, true); opt.onClose && opt.onClose(); };
    const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };
    addEventListener('keydown', onKey, true);
    back.addEventListener('pointerdown', (e) => { if (e.target === back) close(); });
    if (opt.title !== undefined) {
      const head = h('header', {}, h('h2', {}, opt.title));
      if (opt.headerExtra) head.append(opt.headerExtra);
      head.append(h('button', { class: 'ghost', title: 'Close (Esc)', onclick: close, style: opt.headerExtra ? '' : 'margin-left:auto' }, '✕'));
      box.append(head);
    }
    if (opt.body) box.append(opt.body);
    if (opt.footer) box.append(opt.footer);
    back.append(box);
    $('#modalRoot').append(back);
    return { close, box };
  };
  LE.info = (title, html) => LE.modal({ title, cls: 'small', body: h('div', { class: 'content', html }) });

  // ------------------------------------------------------------ menus
  let openMenu = null;
  LE.closeMenu = () => { if (openMenu) { openMenu.el.remove(); openMenu.btn && openMenu.btn.classList.remove('open'); openMenu = null; } };
  // entries: {label, cmd | run, key, check(), disabled(), sub}, '-' or {header}
  LE.showMenu = (entries, x, y, btn) => {
    LE.closeMenu();
    const el = h('div', { class: 'menu', role: 'menu' });
    for (const e of entries) {
      if (e === '-') { el.append(h('hr')); continue; }
      if (e.header) { el.append(h('div', { class: 'mh' }, e.header)); continue; }
      const checked = e.check ? e.check() : null;
      const b = h('button', { class: 'mi', role: 'menuitem', disabled: e.disabled && e.disabled() },
        h('span', { class: 'ck' }, checked === null ? '' : checked ? '✓' : ''), e.label, e.badge ? h('span', { class: 'badge nw' }, e.badge) : null, e.key ? h('span', { class: 'k' }, e.key) : null);
      b.onclick = () => { LE.closeMenu(); e.run ? e.run() : LE.run(e.cmd); };
      el.append(b);
    }
    $('#menuRoot').append(el);
    const r = el.getBoundingClientRect();
    el.style.left = Math.max(4, Math.min(x, innerWidth - r.width - 4)) + 'px';
    el.style.top = Math.max(4, Math.min(y, innerHeight - r.height - 4)) + 'px';
    openMenu = { el, btn };
    if (btn) btn.classList.add('open');
  };
  addEventListener('pointerdown', (e) => { if (openMenu && !openMenu.el.contains(e.target) && e.target !== openMenu.btn) LE.closeMenu(); });

  // ------------------------------------------------------------ commands
  const commands = new Map();
  LE.commands = commands;
  LE.cmd = (id, label, run, opt = {}) => commands.set(id, { id, label, run, ...opt });
  LE.run = (id, ...a) => { const c = commands.get(id); if (c) c.run(...a); else console.warn('no command', id); };

  LE.palette = (initial = '') => {
    const inp = h('input', { type: 'text', placeholder: 'Type a command, a scene, or a setting…', 'aria-label': 'Command palette' });
    const res = h('div', { class: 'res' });
    const body = h('div', { class: 'palette' }, inp, res);
    const m = LE.modal({ body, cls: 'palette' });
    let hl = 0, list = [];
    const entries = () => {
      const out = [];
      for (const c of commands.values()) if (!c.hidden) out.push({ cat: c.cat || 'Command', label: c.label, key: c.key, run: () => c.run() });
      for (const p of LE.engine.index.projects) for (const s of p.scenes) out.push({ cat: 'Scene', label: s.title + '  ·  ' + p.title, run: () => LE.run('openScene', s.name) });
      for (const it of items.values()) out.push({ cat: it.scope === 'editor' ? 'Preference' : 'Setting', label: it.cat.title + ' › ' + it.label, run: () => LE.run(it.scope === 'editor' ? 'editorPrefs' : 'projectSettings', it.id) });
      return out;
    };
    const all = entries();
    const score = (label, q) => {
      if (!q) return 1;
      const l = label.toLowerCase();
      if (l.includes(q)) return 100 - l.indexOf(q);
      let i = 0;
      for (const ch of l) if (ch === q[i]) i++;
      return i === q.length ? 1 : 0;
    };
    const draw = () => {
      const q = inp.value.trim().toLowerCase();
      list = all.map((e) => ({ e, s: score(e.label, q) })).filter((x) => x.s > 0).sort((a, b) => b.s - a.s).slice(0, 80).map((x) => x.e);
      hl = Math.min(hl, Math.max(0, list.length - 1));
      res.innerHTML = '';
      list.forEach((e, i) => {
        const b = h('button', { class: 'mi' + (i === hl ? ' hl' : '') }, h('span', { class: 'cat' }, e.cat), e.label, e.key ? h('span', { class: 'k' }, e.key) : null);
        b.onclick = () => { m.close(); e.run(); };
        res.append(b);
      });
      if (!list.length) res.append(h('div', { class: 'empty' }, 'Nothing matches.'));
    };
    inp.oninput = () => { hl = 0; draw(); };
    inp.onkeydown = (e) => {
      if (e.key === 'ArrowDown') { hl = Math.min(list.length - 1, hl + 1); draw(); res.children[hl]?.scrollIntoView({ block: 'nearest' }); e.preventDefault(); }
      else if (e.key === 'ArrowUp') { hl = Math.max(0, hl - 1); draw(); res.children[hl]?.scrollIntoView({ block: 'nearest' }); e.preventDefault(); }
      else if (e.key === 'Enter' && list[hl]) { m.close(); list[hl].run(); }
    };
    inp.value = initial;
    draw();
    setTimeout(() => inp.focus(), 0);
  };

  // ------------------------------------------------------------ settings windows
  LE.settingsWindow = (scope, focusId) => {
    const cats = LE.SCHEMA[scope];
    const title = scope === 'editor' ? 'Editor Preferences' : 'Project Settings · ' + (LE.engine.projectTitle || store.project);
    const search = h('input', { type: 'search', placeholder: 'Search ' + items.size + ' settings…', 'aria-label': 'Search settings' });
    const catBox = h('nav', { class: 'cats' });
    const sets = h('div', { class: 'sets' });
    const body = h('div', { class: 'mbody' }, catBox, sets);
    let current = cats[0].id;
    const countIn = (cat) => cat.groups.reduce((n, g) => n + g.items.length, 0);
    const modIn = (cat) => cat.groups.reduce((n, g) => n + g.items.filter((it) => store.isModified(it.id)).length, 0);
    const drawCats = () => {
      catBox.innerHTML = '';
      for (const c of cats) {
        const mods = modIn(c);
        catBox.append(h('button', { class: c.id === current && !search.value ? 'active' : '', onclick: () => { current = c.id; search.value = ''; draw(); } },
          h('span', { class: 'ic' }, c.icon), c.title, h('span', { class: 'n', title: mods ? mods + ' changed' : '' }, mods ? '● ' + countIn(c) : countIn(c))));
      }
    };
    const draw = () => {
      drawCats();
      sets.innerHTML = '';
      const q = search.value.trim().toLowerCase();
      if (q) {
        let hits = 0;
        for (const c of cats) for (const g of c.groups) {
          const rows = g.items.filter((it) => (it.label + ' ' + it.id + ' ' + (it.help || '') + ' ' + g.title + ' ' + c.title).toLowerCase().includes(q)).map((it) => propRow(it, { showHelp: true }));
          if (rows.length) { hits += rows.length; sets.append(propGroup(c.title + ' › ' + g.title, rows)); }
        }
        if (!hits) sets.append(h('div', { class: 'empty' }, 'No setting matches "' + search.value + '".'));
        return;
      }
      const c = cats.find((x) => x.id === current);
      const reset = h('button', { onclick: () => { store.resetMany(c.groups.flatMap((g) => g.items.map((i) => i.id))); draw(); LE.toast('Reset ' + c.title); } }, 'Reset section');
      sets.append(h('h3', {}, h('span', {}, c.icon), c.title, h('span', { class: 'act' }, reset)));
      for (const g of c.groups) sets.append(propGroup(g.title, g.items.map((it) => propRow(it, { showHelp: true }))));
    };
    search.oninput = draw;
    const fileIn = h('input', { type: 'file', accept: '.json,application/json', hidden: true });
    fileIn.onchange = async () => {
      const f = fileIn.files[0];
      if (!f) return;
      try { const n = store.importJson(scope, await f.text()); LE.toast('Imported ' + n + ' settings'); draw(); } catch (e) { LE.toast('That file isn\'t a settings file', 'err'); }
    };
    const all = [...items.values()].filter((it) => it.scope === scope);
    const wired = all.filter((it) => store.status(it) === 'live' || store.status(it) === 'build').length;
    const nb = all.filter((it) => store.status(it) === 'nb').length, you = all.filter((it) => store.status(it) === 'you').length;
    const footer = h('footer', {},
      h('span', { class: 'dim' }, all.length + ' settings · ' + wired + ' working · ' + nb + ' not built yet' + (you ? ' · ' + you + ' need you' : '')),
      h('span', { class: 'spacer' }),
      fileIn,
      h('button', { onclick: () => fileIn.click() }, 'Import…'),
      h('button', { onclick: () => LE.download(scope === 'editor' ? 'editor-preferences.json' : store.project + '.settings.json', store.exportJson(scope), 'application/json') }, 'Export…'),
      h('button', { onclick: () => { if (confirm('Reset every ' + (scope === 'editor' ? 'preference' : 'project setting') + ' to its default?')) { store.resetMany(all.map((i) => i.id)); draw(); } } }, 'Reset all'));
    LE.modal({ title, headerExtra: search, body, footer });
    if (focusId && items.has(focusId)) {
      const it = items.get(focusId);
      current = it.cat.id;
      draw();
      const row = sets.querySelector('[data-id="' + CSS.escape(focusId) + '"]');
      if (row) { row.classList.add('hit'); row.scrollIntoView({ block: 'center' }); }
    } else draw();
    setTimeout(() => { if (!matchMedia('(pointer: coarse)').matches) search.focus(); }, 0);
  };

  LE.download = (name, data, type = 'text/plain') => {
    const blob = data instanceof Blob ? data : new Blob([data], { type });
    const a = h('a', { href: URL.createObjectURL(blob), download: name });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
  };

})();
