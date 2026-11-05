// The editor's panels: Project, Outliner, Inspector, Look, Script, Console,
// Content, Timeline and Profiler.
window.LE = window.LE || {};
(() => {
  const { h, $ } = LE;
  const E = LE.engine;
  const P = (LE.panels = {});
  const el = (id) => document.querySelector('.panel[data-panel="' + id + '"]');
  const shown = new Set();
  LE.emitPanel = (id) => { shown.add(id); P[id] && P[id].show && P[id].show(); };
  const visible = (id) => { const p = el(id); return p && !p.hidden && !p.classList.contains('inactive') && p.offsetParent !== null; };
  LE.panelVisible = visible;
  const fmt = (n, d = 2) => (typeof n === 'number' ? n.toFixed(d) : '—');
  const v3s = (v) => (v ? v.map((x) => (+x).toFixed(2)).join(', ') : '—');

  // ============================================================ Project
  P.project = {
    init() {
      const root = el('project');
      this.filter = h('input', { type: 'search', placeholder: 'Find a scene…', 'aria-label': 'Find a scene' });
      this.filter.oninput = () => this.draw();
      this.list = h('div', { style: 'flex:1;overflow:auto' });
      root.append(h('div', { class: 'ptools' }, this.filter, h('button', { title: 'New scene', onclick: () => LE.run('newScene') }, '+ Scene')), this.list);
      E.on('index', () => this.draw());
      E.on('sceneText', () => this.draw());
    },
    draw() {
      const q = (this.filter.value || '').toLowerCase();
      const tree = h('ul', { class: 'tree' });
      for (const p of E.index.projects) {
        const kids = h('ul');
        const scenes = p.scenes.filter((s) => !q || (s.title + ' ' + s.name).toLowerCase().includes(q));
        for (const s of scenes) {
          const row = h('div', { class: 'row' + (s.name === E.scene ? ' sel' : ''), title: s.name + '.loop' + (s.still ? ' · has a trailer still' : '') },
            h('span', { class: 'tw' }), h('span', { class: 'ic' }, s.still ? '🎞' : '◇'), h('span', { class: 'nm' }, s.title),
            s.name === p.start ? h('span', { class: 'meta' }, 'start') : null);
          row.onclick = () => LE.run('openScene', s.name);
          row.oncontextmenu = (e) => { e.preventDefault(); LE.showMenu([{ header: s.name + '.loop' }, { label: 'Open', run: () => LE.run('openScene', s.name) }, { label: 'Open and edit the script', run: () => { LE.run('openScene', s.name); LE.dock.show('script'); } }, { label: 'Copy link', run: () => { navigator.clipboard?.writeText(location.origin + location.pathname.replace(/editor\.html$/, '') + '#' + s.name); LE.toast('Link copied'); } }, { label: 'Play in the player', run: () => open('./#' + s.name, '_blank') }], e.clientX, e.clientY); };
          kids.append(h('li', {}, row));
        }
        for (const lib of p.library || []) if (!q || lib.includes(q)) kids.append(h('li', {}, h('div', { class: 'row', title: 'Shared script (use "' + lib + '")', onclick: () => LE.run('openLibrary', lib) }, h('span', { class: 'tw' }), h('span', { class: 'ic' }, '{}'), h('span', { class: 'nm dim' }, lib + '.loop'), h('span', { class: 'meta' }, 'shared'))));
        const li = h('li', { class: q || p.scenes.some((s) => s.name === E.scene) || E.index.projects.length < 3 ? '' : 'closed' });
        const head = h('div', { class: 'row', title: p.description }, h('span', { class: 'tw' }, '▾'), h('span', { class: 'ic' }, '▤'), h('b', { class: 'nm' }, p.title), h('span', { class: 'meta' }, p.scenes.length));
        head.onclick = () => { li.classList.toggle('closed'); head.firstChild.textContent = li.classList.contains('closed') ? '▸' : '▾'; };
        if (li.classList.contains('closed')) head.firstChild.textContent = '▸';
        li.append(head, kids);
        tree.append(li);
      }
      this.list.innerHTML = '';
      this.list.append(E.index.projects.length ? tree : h('div', { class: 'empty' }, 'No projects found. Build the site with tools/build_site.sh.'));
    },
  };

  // ============================================================ Outliner
  P.outliner = {
    info: null, sel: null, open: { lights: true, people: true, things: false, camera: true },
    init() {
      const root = el('outliner');
      this.filter = h('input', { type: 'search', placeholder: 'Filter…', 'aria-label': 'Filter the outliner' });
      this.filter.oninput = () => this.draw();
      this.list = h('div', { style: 'flex:1;overflow:auto' });
      this.count = h('span', { class: 'dim' });
      root.append(h('div', { class: 'ptools' }, this.filter, h('button', { title: 'Refresh', onclick: () => this.refresh() }, '⟳')), this.list);
      E.on('shown', () => this.refresh());
      E.on('started', () => { this.sel = null; setTimeout(() => this.refresh(), 300); });
      setInterval(() => { if (visible('outliner') || visible('inspector') || visible('timeline')) this.refresh(true); }, 2000);
    },
    show() { this.refresh(); },
    refresh(quiet) {
      const info = E.M && E.sceneInfo();
      if (!info) return;
      const sig = JSON.stringify([info.things.length, info.lights.length, info.people.length, info.title]);
      this.info = info;
      LE.emitSceneInfo && LE.emitSceneInfo(info);
      if (quiet && sig === this.sig) { P.inspector.update(); return; }
      this.sig = sig;
      this.draw();
      P.inspector.update();
    },
    select(kind, i) { this.sel = { kind, i }; this.draw(); P.inspector.draw(); if (!LE.panelVisible('inspector')) LE.dock.show('inspector'); },
    draw() {
      const info = this.info;
      this.list.innerHTML = '';
      if (!info) { this.list.append(h('div', { class: 'empty' }, 'Load a scene to see what\'s in it.')); return; }
      const q = (this.filter.value || '').toLowerCase();
      const tree = h('ul', { class: 'tree' });
      const leaf = (ic, name, meta, kind, i, title) => {
        const isSel = this.sel && this.sel.kind === kind && this.sel.i === i;
        const r = h('div', { class: 'row' + (isSel ? ' sel' : ''), title: title || name }, h('span', { class: 'tw' }), h('span', { class: 'ic' }, ic), h('span', { class: 'nm' }, name), meta ? h('span', { class: 'meta' }, meta) : null);
        r.onclick = () => this.select(kind, i);
        return h('li', {}, r);
      };
      const branch = (key, ic, name, n, kids) => {
        const li = h('li', { class: this.open[key] || q ? '' : 'closed' });
        const head = h('div', { class: 'row' }, h('span', { class: 'tw' }, this.open[key] || q ? '▾' : '▸'), h('span', { class: 'ic' }, ic), h('b', { class: 'nm' }, name), h('span', { class: 'meta' }, n));
        head.onclick = () => { this.open[key] = !this.open[key]; this.draw(); };
        li.append(head, h('ul', {}, kids));
        return li;
      };
      const sceneRow = h('div', { class: 'row' + (this.sel && this.sel.kind === 'scene' ? ' sel' : '') }, h('span', { class: 'tw' }), h('span', { class: 'ic' }, '◈'), h('b', { class: 'nm' }, info.title || 'Scene'), h('span', { class: 'meta' }, info.mode));
      sceneRow.onclick = () => this.select('scene', 0);
      tree.append(h('li', {}, sceneRow));
      tree.append(leaf('◎', 'Camera', info.camera ? info.camera.focalLength.toFixed(0) + ' mm' : '', 'camera', 0));
      if (info.shots) tree.append(leaf('🎬', 'Film', info.shots + ' shots · ' + fmt(info.length, 1) + ' s', 'film', 0));
      const lights = info.lights.map((l, i) => ({ l, i })).filter(({ l }) => !q || ('light ' + l.kind).includes(q));
      tree.append(branch('lights', '☀', 'Lights', info.lights.length, lights.map(({ l, i }) => leaf(l.kind === 'sun' ? '☀' : l.kind === 'spot' ? '◒' : l.kind === 'rect' ? '▭' : '💡', l.kind[0].toUpperCase() + l.kind.slice(1) + ' ' + (i + 1), l.kind === 'sun' ? Math.round(l.lux) + ' lux' : Math.round(l.lumens) + ' lm', 'light', i))));
      tree.append(branch('people', '☺', 'People', info.people.length, info.people.map((p, i) => leaf('☺', 'Person ' + (i + 1), p.walking ? 'walking' : '', 'person', i)).filter((x, i) => !q || ('person ' + (i + 1)).includes(q))));
      // things grouped by name (or kind)
      const kinds = { b: 'Box', s: 'Sphere', c: 'Capsule', m: 'Mesh' };
      const groups = new Map();
      info.things.forEach((t, i) => {
        const nm = t.name || kinds[t.kind] || 'Thing';
        if (q && !(nm + ' ' + (t.material || '')).toLowerCase().includes(q)) return;
        if (!groups.has(nm)) groups.set(nm, []);
        groups.get(nm).push(i);
      });
      const thingKids = [];
      for (const [nm, list] of groups) {
        if (list.length === 1) { const t = info.things[list[0]]; thingKids.push(leaf(t.solid ? '◼' : '◻', nm, t.material || '', 'thing', list[0], nm + (t.material ? ' · ' + t.material : ''))); continue; }
        const key = 'g:' + nm;
        thingKids.push(branch(key, '▣', nm, list.length, list.slice(0, 400).map((i) => leaf(info.things[i].solid ? '◼' : '◻', nm + ' ' + (list.indexOf(i) + 1), info.things[i].material || '', 'thing', i))));
      }
      tree.append(branch('things', '▦', 'Things', info.things.length + (info.instances > info.things.length ? ' · ' + info.instances + ' drawn' : ''), thingKids));
      tree.append(leaf('♪', 'Sounds', String(info.sounds), 'sounds', 0));
      this.list.append(tree);
    },
  };

  // ============================================================ Inspector
  P.inspector = {
    init() {
      const root = el('inspector');
      this.body = h('div', { style: 'flex:1;overflow:auto' });
      root.append(this.body);
      this.draw();
    },
    show() { this.draw(); },
    update() { if (visible('inspector')) this.draw(true); },
    ro(label, value) { return h('div', { class: 'prop' }, h('div', { class: 'lbl' }, h('i', { class: 'mod' }), label), h('div', { class: 'ctl mono' }, String(value)), h('div', { class: 'side' })); },
    // an editable-looking field for object properties (editing isn't wired yet)
    field(label, value, type = 'vec3') {
      const it = { id: 'obj.' + label, label, type, def: value, min: -1e6, max: 1e6, step: 0.01, bind: null };
      return LE.propRow(it, { get: () => value, set: () => LE.toast('Editing objects isn\'t wired to the engine yet. Change it in the Script tab for now.'), isModified: () => false });
    },
    draw(quiet) {
      if (quiet && document.activeElement && this.body.contains(document.activeElement)) return;
      const info = P.outliner.info, sel = P.outliner.sel;
      const b = this.body;
      const scrollTop = b.scrollTop;
      b.innerHTML = '';
      if (!info) { b.append(h('div', { class: 'empty' }, 'Nothing selected.')); return; }
      if (!sel || sel.kind === 'scene') {
        b.append(LE.propGroup('Scene', [this.ro('Title', info.title), this.ro('Mode', info.mode), this.ro('Things', info.things.length + ' (' + info.instances + ' drawn, ' + info.meshes + ' shapes)'), this.ro('Lights', info.lights.length), this.ro('People', info.people.length), this.ro('Sounds', info.sounds), this.ro('Shots', info.shots + (info.shots ? ' · ' + fmt(info.length, 1) + ' s' : ''))]));
        b.append(LE.propGroup('Look (scene)', [this.ro('Look', info.look.name), this.ro('Bloom', fmt(info.look.bloom, 3)), this.ro('Vignette', fmt(info.look.vignette)), this.ro('Contrast', fmt(info.look.contrast)), this.ro('Saturation', fmt(info.look.saturation))], { action: h('button', { onclick: () => LE.dock.show('look') }, 'Override…') }));
        b.append(LE.propGroup('Fog (scene)', [this.ro('Density', fmt(info.fog.density, 4) + ' /m'), this.ro('Colour', v3s(info.fog.albedo))]));
        if (info.player) b.append(LE.propGroup('You (the player)', [this.ro('Position', v3s(info.player.position)), this.ro('Facing', (info.player.yaw * 57.2958).toFixed(0) + '°'), this.ro('Looking up', (info.player.pitch * 57.2958).toFixed(0) + '°')]));
      } else if (sel.kind === 'thing') {
        const t = info.things[sel.i];
        if (!t) return;
        b.append(h('div', { class: 'notice' }, 'Moving and editing things from the editor ', h('span', { class: 'badge nw' }, 'Not wired'), ' comes next. Today, change them in the Script tab and press Run.'));
        b.append(LE.propGroup('Transform', [this.field('Position', t.position), this.field('Turn (yaw, pitch, roll)', t.rotation.map((r) => +(r * 57.2958).toFixed(1))), this.field('Scale', t.scale)]));
        b.append(LE.propGroup('Thing', [this.ro('Name', t.name || '—'), this.ro('Shape', { b: 'box', s: 'sphere', c: 'capsule', m: 'mesh' }[t.kind] || t.kind), this.ro('Material', t.material || '—'), this.ro('Visible', t.visible ? 'yes' : 'no')]));
        b.append(LE.propGroup('Physics', [this.ro('Solid', t.solid ? 'yes (falls, rolls, knocks)' : 'no')]));
      } else if (sel.kind === 'light') {
        const l = info.lights[sel.i];
        if (!l) return;
        b.append(h('div', { class: 'notice' }, 'Editing lights here ', h('span', { class: 'badge nw' }, 'Not wired'), ' comes next.'));
        const rows = [this.ro('Kind', l.kind), this.field('Position', l.position), this.field('Direction', l.direction), this.field('Colour', l.color)];
        rows.push(l.kind === 'sun' ? this.field('Brightness (lux)', l.lux, 'number') : this.field('Brightness (lumens)', l.lumens, 'number'));
        rows.push(this.ro('Casts shadows', l.shadows ? 'yes' : 'no'));
        b.append(LE.propGroup('Light', rows));
      } else if (sel.kind === 'camera') {
        const c = info.camera;
        b.append(LE.propGroup('Scene camera', [this.ro('Position', v3s(c.position)), this.ro('Looks at', v3s(c.target)), this.ro('Lens', fmt(c.focalLength, 0) + ' mm'), this.ro('Aperture', 'f/' + fmt(c.fStop, 1)), this.ro('ISO', fmt(c.iso, 0)), this.ro('Auto exposure', c.autoExposure ? 'yes' : 'no')]));
        const ids = ['cam.autoExposure', 'cam.exposureCompensation', 'cam.focalLength', 'cam.fStop', 'cam.focusDistance', 'cam.iso', 'cam.blades'];
        b.append(LE.propGroup('Override (live)', ids.map((id) => LE.propRow(LE.store.items.get(id)))));
      } else if (sel.kind === 'person') {
        const p = info.people[sel.i];
        b.append(LE.propGroup('Person ' + (sel.i + 1), [this.ro('Walking', p.walking ? 'yes' : 'no'), this.ro('Speed', fmt(p.speed, 1) + ' m/s'), this.ro('Visible', p.visible ? 'yes' : 'no')]));
        b.append(LE.propGroup('Body', [LE.propRow(LE.store.items.get('anim.ik')), LE.propRow(LE.store.items.get('anim.steering')), LE.propRow(LE.store.items.get('anim.faces'))]));
      } else if (sel.kind === 'film') {
        b.append(LE.propGroup('Film', [this.ro('Shots', info.shots), this.ro('Length', fmt(info.length, 1) + ' s')], { action: h('button', { onclick: () => LE.dock.show('timeline') }, 'Timeline') }));
      } else if (sel.kind === 'sounds') {
        b.append(LE.propGroup('Sounds', [this.ro('Playing', info.sounds)]));
        b.append(LE.propGroup('Mix', ['audio.master', 'audio.ambience', 'audio.effects', 'audio.footsteps', 'audio.reverb'].map((id) => LE.propRow(LE.store.items.get(id)))));
      }
      b.scrollTop = scrollTop;
    },
  };

  // ============================================================ Look (live overrides)
  P.look = {
    init() {
      const root = el('look');
      const presets = h('div', { class: 'ptools' });
      presets.append(h('select', { 'aria-label': 'Look preset', onchange: (e) => { LE.store.set('look.preset', e.target.value); this.draw(); } },
        h('option', { value: '' }, "Scene's own look"), ...LE.LOOKS.map((l) => h('option', { value: l }, l[0].toUpperCase() + l.slice(1)))));
      presets.append(h('button', { title: 'Reset every override', onclick: () => { const ids = this.ids(); LE.store.resetMany(ids); this.draw(); LE.toast('Back to the scene\'s own look'); } }, 'Reset'));
      this.presetSel = presets.firstChild;
      this.body = h('div', { style: 'flex:1;overflow:auto' });
      root.append(presets, this.body);
      LE.store.on((id) => { if (id === 'look.preset') this.presetSel.value = LE.store.get('look.preset'); });
      this.draw();
    },
    ids() { return [...LE.store.items.values()].filter((it) => it.cat.id === 'film' || it.cat.id === 'camera' || (it.cat.id === 'sky' && it.bind)).map((it) => it.id); },
    draw() {
      this.presetSel.value = LE.store.get('look.preset');
      this.body.innerHTML = '';
      const film = LE.SCHEMA.project.find((c) => c.id === 'film');
      for (const g of film.groups) this.body.append(LE.propGroup(g.title, g.items.filter((it) => it.id !== 'look.preset').map((it) => LE.propRow(it, { noBadge: true })), { open: ['Look', 'Glow', 'Lens'].includes(g.title) }));
      const cam = LE.SCHEMA.project.find((c) => c.id === 'camera');
      this.body.append(LE.propGroup('Exposure', cam.groups[0].items.filter((it) => it.bind).map((it) => LE.propRow(it, { noBadge: true })), { open: false }));
      this.body.append(LE.propGroup('Lens', cam.groups[1].items.filter((it) => it.bind).map((it) => LE.propRow(it, { noBadge: true })), { open: false }));
      const sky = LE.SCHEMA.project.find((c) => c.id === 'sky');
      this.body.append(LE.propGroup('Fog', sky.groups[0].items.map((it) => LE.propRow(it, { noBadge: true })), { open: false }));
    },
  };

  // ============================================================ Script
  const KW = new Set(['after', 'and', 'break', 'by', 'continue', 'each', 'elif', 'else', 'emit', 'every', 'fn', 'for', 'forever', 'from', 'if', 'in', 'is', 'let', 'not', 'or', 'otherwise', 'repeat', 'return', 'task', 'times', 'to', 'wait', 'when', 'while', 'use']);
  const CONST = new Set(['true', 'false', 'yes', 'no', 'nothing', 'nil', 'pi', 'tau', 'infinity']);
  const WORLD = new Set(('title mode material box sphere cylinder cone capsule ring ground rock tree terrain forest height_at light panel_light sun sky fog camera look shot orbit solid push gravity person sound room line ask go_to remember recall forget_story player near move_player render film say print vec rgb hex kelvin grey materials ease approach random random_int chance seed noise wave flicker time text number type range keys list').split(' '));
  const UNITS = 'm|cm|mm|km|s|ms|sec|min|minutes|h|hour|hours|deg|degrees|rad|K|lumens|lm|lux|nits|kg|g|Hz|kmh|mph|%';
  const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;');
  function highlight(src, errLines) {
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
      out.push(errLines.has(i + 1) ? '<span class="errline">' + (html || ' ') + '</span>' : html);
    });
    return out.join('\n') + '\n';
  }
  P.script = {
    name: null, dirty: false, errLines: new Set(), problems: [],
    init() {
      const root = el('script');
      this.title = h('span', { class: 'dim mono' }, 'no scene');
      this.runBtn = h('button', { class: 'primary', title: 'Run this script (Ctrl+Enter)', onclick: () => this.run() }, '▶ Run');
      this.revertBtn = h('button', { title: 'Go back to the file as it is on the site', onclick: () => this.revert() }, 'Revert');
      root.append(h('div', { class: 'ptools' }, this.title, h('span', { class: 'spacer' }), h('button', { title: 'Find (Ctrl+F)', onclick: () => this.find() }, 'Find'), h('button', { title: 'Download this .loop file (Ctrl+Shift+S)', onclick: () => this.download() }, 'Save .loop'), this.revertBtn, this.runBtn));
      this.gutter = h('div', { class: 'gutter', 'aria-hidden': 'true' });
      this.pre = h('pre', { 'aria-hidden': 'true' });
      this.ta = h('textarea', { spellcheck: 'false', autocapitalize: 'off', autocomplete: 'off', 'aria-label': 'Looped Script' });
      this.area = h('div', { class: 'area' }, this.pre, this.ta);
      this.probBox = h('div', { class: 'problems' });
      root.append(h('div', { class: 'code' }, this.gutter, this.area), this.probBox);
      this.ta.addEventListener('input', () => { this.dirty = true; this.paint(); this.autosave(); });
      this.area.addEventListener('scroll', () => { this.gutter.scrollTop = this.area.scrollTop; });
      this.ta.addEventListener('keydown', (e) => this.keys(e));
      E.on('sceneText', (name, text) => {
        const kept = LE.store.get('save.auto') && LE.safe.get('loop3d.script.' + name);
        this.load(name, kept && kept !== text ? kept : text, !!(kept && kept !== text));
      });
      E.on('problems', (t) => this.setProblems(t));
      E.on('started', () => { if (!this.pendingProblems) this.setProblems(''); this.pendingProblems = false; });
    },
    show() { this.paint(); },
    load(name, text, restored) {
      this.name = name;
      this.ta.value = text;
      this.dirty = !!restored;
      this.title.textContent = name + '.loop' + (restored ? ' · your edits (kept in this browser)' : '');
      this.paint();
      if (restored) LE.toast('Restored your edits to ' + name + '.loop · Revert to drop them');
    },
    setProblems(t) {
      this.problems = t ? t.trim().split('\n').filter(Boolean) : [];
      this.errLines = new Set();
      this.probBox.innerHTML = '';
      for (const p of this.problems) {
        const m = p.match(/line (\d+)/);
        const n = m ? +m[1] - E.lineOffset : 0;
        const text = m ? (n > 0 ? p.replace(/scene\.loop line \d+/, (this.name || 'scene') + '.loop line ' + n) : p.replace(/scene\.loop line \d+/, 'a shared script (use "…")')) : p;
        if (n > 0) this.errLines.add(n);
        const d = h('div', { title: n > 0 ? 'Go to the line' : '' }, '⚠ ' + text);
        if (n > 0) d.onclick = () => this.gotoLine(n);
        this.probBox.append(d);
      }
      this.paint();
      LE.status && LE.status();
    },
    paint() {
      const text = this.ta.value;
      const lines = text.split('\n').length;
      let g = '';
      for (let i = 1; i <= lines; i++) g += (this.errLines.has(i) ? '<span class="err">' + i + '</span>' : i) + '\n';
      this.gutter.innerHTML = g;
      this.pre.innerHTML = highlight(text, this.errLines);
      // the textarea grows with its text; the area scrolls both
      this.ta.style.height = '0px';
      this.ta.style.height = Math.max(this.area.clientHeight, this.ta.scrollHeight) + 'px';
      this.ta.style.width = '0px';
      this.ta.style.width = Math.max(this.area.clientWidth, this.pre.scrollWidth) + 'px';
      LE.status && LE.status();
    },
    autosave() {
      clearTimeout(this.saveT);
      if (!LE.store.get('save.auto') || !this.name) return;
      this.saveT = setTimeout(() => { LE.safe.set('loop3d.script.' + this.name, this.ta.value); LE.saved = new Date(); LE.status && LE.status(); }, LE.store.get('save.every') * 1000);
    },
    keys(e) {
      const ta = this.ta, tab = ' '.repeat(LE.store.get('code.tab'));
      if ((e.ctrlKey || e.metaKey) && (e.key === 's' || e.key === 'Enter')) {
        e.preventDefault();
        if (e.shiftKey && e.key === 's') return this.download();
        LE.safe.set('loop3d.script.' + this.name, ta.value);
        if (e.key === 'Enter' || LE.store.get('code.runOnSave')) this.run();
        else LE.toast('Kept in this browser');
        return;
      }
      if ((e.ctrlKey || e.metaKey) && e.key === 'f') { e.preventDefault(); return this.find(); }
      if ((e.ctrlKey || e.metaKey) && e.key === '/') {
        e.preventDefault();
        const s = ta.value.lastIndexOf('\n', ta.selectionStart - 1) + 1;
        let end = ta.value.indexOf('\n', ta.selectionEnd); if (end < 0) end = ta.value.length;
        const block = ta.value.slice(s, end).split('\n');
        const allC = block.every((l) => /^\s*#/.test(l) || !l.trim());
        const nb = block.map((l) => (allC ? l.replace(/^(\s*)# ?/, '$1') : l.trim() ? l.replace(/^(\s*)/, '$1# ') : l)).join('\n');
        ta.setRangeText(nb, s, end, 'select');
        ta.dispatchEvent(new Event('input'));
        return;
      }
      if (e.key === 'Tab') {
        e.preventDefault();
        const s = ta.value.lastIndexOf('\n', ta.selectionStart - 1) + 1;
        if (ta.selectionStart === ta.selectionEnd && !e.shiftKey) { ta.setRangeText(tab, ta.selectionStart, ta.selectionEnd, 'end'); }
        else {
          let end = ta.selectionEnd; const block = ta.value.slice(s, end).split('\n');
          const nb = block.map((l) => (e.shiftKey ? l.replace(new RegExp('^ {1,' + tab.length + '}'), '') : tab + l)).join('\n');
          ta.setRangeText(nb, s, end, 'select');
        }
        ta.dispatchEvent(new Event('input'));
        return;
      }
      if (e.key === 'Enter' && LE.store.get('code.autoIndent')) {
        const s = ta.value.lastIndexOf('\n', ta.selectionStart - 1) + 1;
        const line = ta.value.slice(s, ta.selectionStart);
        let ind = line.match(/^\s*/)[0];
        if (/^\s*(if|else|elif|otherwise|for|while|repeat|when|every|after|to|task)\b.*$/.test(line) && !/:\s*\S/.test(line) || /:\s*$/.test(line)) ind += tab;
        e.preventDefault();
        ta.setRangeText('\n' + ind, ta.selectionStart, ta.selectionEnd, 'end');
        ta.dispatchEvent(new Event('input'));
      }
    },
    gotoLine(n) {
      LE.dock.show('script');
      const lines = this.ta.value.split('\n');
      const pos = lines.slice(0, n - 1).reduce((a, l) => a + l.length + 1, 0);
      this.ta.focus();
      this.ta.setSelectionRange(pos, pos + (lines[n - 1] || '').length);
      const lh = parseFloat(getComputedStyle(this.ta).lineHeight) || 19;
      this.area.scrollTop = Math.max(0, (n - 5) * lh);
    },
    find() {
      const q = prompt('Find', this.lastFind || '');
      if (!q) return;
      this.lastFind = q;
      const from = this.ta.selectionEnd;
      let i = this.ta.value.indexOf(q, from);
      if (i < 0) i = this.ta.value.indexOf(q);
      if (i < 0) return LE.toast('Not found: ' + q);
      this.gotoLine(this.ta.value.slice(0, i).split('\n').length);
      this.ta.setSelectionRange(i, i + q.length);
    },
    async run() {
      if (!E.M) return;
      this.pendingProblems = false;
      this.setProblems('');
      LE.log('cmd', 'Run ' + (this.name || 'script') + '.loop');
      await E.runText(this.ta.value, this.name);
    },
    async revert() {
      if (!this.name) return;
      if (this.dirty && !confirm('Drop your edits to ' + this.name + '.loop?')) return;
      LE.safe.set('loop3d.script.' + this.name, '');
      try { localStorage.removeItem('loop3d.script.' + this.name); } catch (e) { /* ignore */ }
      const text = await E.fetchScene(this.name);
      this.load(this.name, text, false);
      this.run();
    },
    download() { LE.download((this.name || 'scene') + '.loop', this.ta.value); },
  };

  // ============================================================ Console
  P.console = {
    entries: [], filter: { info: true, warn: true, error: true, say: true, line: true, engine: true, cmd: true },
    init() {
      const root = el('console');
      this.search = h('input', { type: 'search', placeholder: 'Filter messages…', 'aria-label': 'Filter messages' });
      this.search.oninput = () => this.draw();
      const filters = h('span', { class: 'filters' });
      for (const [k, label] of [['error', 'Errors'], ['warn', 'Warnings'], ['engine', 'Engine'], ['line', 'Lines'], ['say', 'Say'], ['info', 'Info']]) {
        const b = h('button', { 'aria-pressed': 'true', onclick: () => { this.filter[k] = !this.filter[k]; b.setAttribute('aria-pressed', this.filter[k]); this.draw(); } }, label);
        filters.append(b);
      }
      this.list = h('div', { class: 'log', role: 'log' });
      this.input = h('input', { type: 'text', placeholder: 'Command: help, scene cabin, quality 2, look noir, set res 0.5, run, clear…', 'aria-label': 'Console command' });
      this.input.onkeydown = (e) => {
        if (e.key === 'Enter' && this.input.value.trim()) { this.history.unshift(this.input.value); this.hi = -1; LE.consoleCommand(this.input.value.trim()); this.input.value = ''; }
        if (e.key === 'ArrowUp') { this.hi = Math.min(this.history.length - 1, this.hi + 1); this.input.value = this.history[this.hi] || ''; e.preventDefault(); }
        if (e.key === 'ArrowDown') { this.hi = Math.max(-1, this.hi - 1); this.input.value = this.history[this.hi] || ''; e.preventDefault(); }
      };
      this.history = []; this.hi = -1;
      root.append(h('div', { class: 'ptools' }, this.search, filters, h('button', { onclick: () => { this.entries = []; this.draw(); } }, 'Clear'), h('button', { title: 'Copy all', onclick: () => { navigator.clipboard?.writeText(this.entries.map((e) => e.text).join('\n')); LE.toast('Copied'); } }, 'Copy')), this.list, h('div', { class: 'cmdline' }, h('span', { class: 'dim mono' }, '›'), this.input));
    },
    add(kind, text) {
      if (kind === 'engine' && !LE.store.get('con.engine')) return;
      if (kind === 'line' && !LE.store.get('con.lines')) return;
      const e = { kind, text: String(text), t: new Date() };
      this.entries.push(e);
      const max = LE.store.get('con.max');
      if (this.entries.length > max) this.entries.splice(0, this.entries.length - max);
      if (this.list && this.match(e)) {
        const stick = this.list.scrollTop + this.list.clientHeight >= this.list.scrollHeight - 8;
        this.list.append(this.row(e));
        while (this.list.childElementCount > max) this.list.firstChild.remove();
        if (stick) this.list.scrollTop = this.list.scrollHeight;
      }
      LE.status && LE.status();
    },
    match(e) { const q = (this.search?.value || '').toLowerCase(); return this.filter[e.kind] !== false && (!q || e.text.toLowerCase().includes(q)); },
    row(e) { return h('div', { class: e.kind }, h('span', { class: 't' }, e.t.toTimeString().slice(0, 8)), e.text); },
    draw() { this.list.innerHTML = ''; this.entries.filter((e) => this.match(e)).forEach((e) => this.list.append(this.row(e))); this.list.scrollTop = this.list.scrollHeight; },
    counts() { return { error: this.entries.filter((e) => e.kind === 'error').length, warn: this.entries.filter((e) => e.kind === 'warn').length }; },
  };
  LE.log = (kind, text) => P.console.add(kind, text);

  // ============================================================ Content
  const MATERIALS = {
    wood: [['oak', '#9a6b3f'], ['pine', '#c89a62'], ['walnut', '#5b3a24'], ['cherry', '#8e4a2c'], ['floorboards', '#86603c'], ['old wood', '#6d5a46'], ['bark', '#4a3a2c']],
    'stone & building': [['marble', '#e6e2dc'], ['granite', '#7e7b78'], ['stone', '#8c8780'], ['brick', '#9a4b35'], ['concrete', '#9c9a95'], ['plaster', '#e3ddd2'], ['tiles', '#cfd5d8'], ['porcelain', '#f3f2ee'], ['clay', '#b5643c']],
    metals: [['gold', '#e3b448'], ['silver', '#d8d8dc'], ['copper', '#c47a4f'], ['brass', '#c9a248'], ['bronze', '#a3713a'], ['iron', '#5e5c5a'], ['steel', '#9aa0a6'], ['chrome', '#e8ecf0'], ['aluminium', '#c2c6ca'], ['rust', '#8a4126']],
    'glass, water & ice': [['glass', '#cfe8ef'], ['crystal', '#e6f6fb'], ['frosted glass', '#dbe7ea'], ['green glass', '#7fb79a'], ['water', '#3d7f9c'], ['ice', '#cbe7f2']],
    'soft things': [['fabric', '#8a6f8f'], ['velvet', '#6b1f33'], ['wool', '#cdbfa8'], ['leather', '#6a3f24'], ['rubber', '#2a2a2a'], ['plastic', '#d84e4e'], ['car paint', '#b0162a'], ['paper', '#f2eee4'], ['wax', '#efe1c0'], ['skin', '#d7a283'], ['hair', '#3a2a1e']],
    nature: [['leaves', '#4f7a34'], ['needles', '#2f5232'], ['autumn leaves', '#c26a2a'], ['grass', '#5f8f3a'], ['soil', '#5a4430'], ['sand', '#d8c08c'], ['snow', '#f4f7fb'], ['moss', '#5c7a2e']],
    glowing: [['flame', '#ffb347'], ['ember', '#ff5a1f'], ['neon', '#ff3cc8'], ['screen', '#8fd0ff'], ['lamp shade', '#ffe2a8']],
    plain: [['white', '#f2f2f2'], ['black', '#141414'], ['grey', '#808080']],
  };
  const SOUNDS = ['fire', 'wind', 'rain', 'stream', 'birds', 'crickets', 'night', 'hum', 'applause', 'crowd', 'strings', 'chains', 'synth', 'drones', 'heartbeat'];
  const ONESHOTS = ['footstep', 'impact', 'bell', 'click', 'thud', 'whoosh'];
  const SHAPES = [
    ['Box', 'box size: vec(1, 1, 1), at: vec(0, 0.5, 0), material: "oak"'], ['Sphere', 'sphere radius: 0.5, at: vec(0, 0.5, 0), material: "chrome"'],
    ['Cylinder', 'cylinder radius: 0.3, height: 1, at: vec(0, 0.5, 0)'], ['Cone', 'cone radius: 0.4, height: 1, at: vec(0, 0.5, 0)'],
    ['Capsule', 'capsule radius: 0.3, height: 1.2, at: vec(0, 0.6, 0)'], ['Ring', 'ring radius: 1, thickness: 0.1, at: vec(0, 1, 0)'],
    ['Ground', 'ground 40, material: "grass"'], ['Rock', 'rock size: 1.5, seed: 3, at: vec(2, 0, 0)'], ['Tree', 'tree kind: "oak", at: vec(4, 0, -3), height: 6 m'],
    ['Terrain', 'terrain size: 400 m, height: 60 m, erosion: 1, seed: 7'], ['Forest', 'forest kind: "pine", around: vec(0, 0, 0), radius: 120 m, spacing: 6 m'],
    ['Lamp', 'light at: vec(0, 2.5, 0), lumens: 800, kelvin: 2700 K'], ['Spotlight', 'light at: vec(0, 3, 0), aim: vec(0, 0, 0), angle: 30 deg, lumens: 1200'],
    ['Panel light', 'panel_light at: vec(0, 3, 0), facing: vec(0, -1, 0), size: vec(1, 1, 0), lumens: 3000'], ['Sun', 'sun elevation: 35 deg, toward: 200 deg'],
    ['Person', 'let p = person(at: vec(0, 0, 2), facing: 180 deg)'], ['Solid ball', 'let ball = sphere(radius: 0.2, at: vec(0, 2, 0), material: "rubber")\nsolid ball, mass: 0.5, bounce: 0.7'],
    ['Shot', 'shot "wide", length: 4 s, from: {at: vec(0, 1.6, 6), look_at: vec(0, 1, 0)}, to: {at: vec(2, 1.6, 4), look_at: vec(0, 1, 0)}'],
    ['Orbit', 'orbit "around", length: 8 s, around: vec(0, 1, 0), radius: 5 m, height: 2 m'],
    ['Line', 'wait line("Mira", "Did you hear that?")'], ['Choice', 'ask "Which way?", ["Left", "Right"]\nwhen choice\n  say "picked {event}"'],
    ['Fog', 'fog density: 0.02, top: 8 m'], ['Every', 'every 2 s\n  say "tick"'],
  ];
  P.content = {
    tab: 'materials',
    init() {
      const root = el('content');
      const tabs = h('span', { class: 'filters' });
      this.tabBtns = {};
      for (const [k, label] of [['materials', 'Materials'], ['shapes', 'Shapes & words'], ['looks', 'Looks'], ['sounds', 'Sounds']]) {
        const b = h('button', { 'aria-pressed': String(k === this.tab), onclick: () => { this.tab = k; for (const [kk, bb] of Object.entries(this.tabBtns)) bb.setAttribute('aria-pressed', String(kk === k)); this.draw(); } }, label);
        this.tabBtns[k] = b; tabs.append(b);
      }
      this.search = h('input', { type: 'search', placeholder: 'Find…', 'aria-label': 'Find content' });
      this.search.oninput = () => this.draw();
      this.body = h('div', { style: 'flex:1;overflow:auto' });
      root.append(h('div', { class: 'ptools' }, tabs, this.search), this.body, h('div', { class: 'dim', style: 'padding:4px 8px;font-size:11px;border-top:1px solid var(--line)' }, 'Click to put it into the script at the cursor.'));
      this.draw();
    },
    insert(code) {
      const s = P.script;
      const ta = s.ta;
      const at = ta.selectionStart;
      const lineStart = ta.value.lastIndexOf('\n', at - 1) + 1;
      const atLineStart = at === lineStart;
      ta.setRangeText((atLineStart ? '' : '\n') + code + '\n', at, ta.selectionEnd, 'end');
      ta.dispatchEvent(new Event('input'));
      LE.dock.show('script');
      LE.toast('Added to ' + (s.name || 'the script') + ' · press Run');
    },
    draw() {
      const q = (this.search.value || '').toLowerCase();
      const b = this.body;
      b.innerHTML = '';
      if (this.tab === 'materials') {
        for (const [cat, list] of Object.entries(MATERIALS)) {
          const items = list.filter(([n]) => !q || n.includes(q));
          if (!items.length) continue;
          b.append(h('div', { class: 'dim', style: 'padding:8px 8px 0;text-transform:capitalize' }, cat));
          const g = h('div', { class: 'grid' });
          for (const [n, c] of items) g.append(h('button', { class: 'tile', title: 'material("' + n + '")', onclick: () => this.insert('let my_' + n.replace(/ /g, '_') + ' = material("' + n + '")') },
            h('div', { class: 'sw', style: 'background:radial-gradient(circle at 35% 30%, #fff8, ' + c + ' 45%, #000 120%)' }), h('div', { class: 'nm' }, n)));
          b.append(g);
        }
      } else if (this.tab === 'shapes') {
        const g = h('div', { class: 'grid' });
        for (const [n, code] of SHAPES.filter(([n, c]) => !q || (n + c).toLowerCase().includes(q))) g.append(h('button', { class: 'tile', title: code, onclick: () => this.insert(code) }, h('div', { class: 'sw', style: 'display:grid;place-items:center;font-size:22px;background:var(--bg2)' }, n[0]), h('div', { class: 'nm' }, n)));
        b.append(g);
      } else if (this.tab === 'looks') {
        const g = h('div', { class: 'grid' });
        const tint = { natural: '#8aa', blockbuster: '#2a8a9a', noir: '#555', golden: '#d9a24a', bleach: '#9aa3a0', seventies: '#b97a3c', neon: '#c03ad0', dream: '#c8a8e0', documentary: '#8a9480', horror: '#3e5a3a', arctic: '#8fc0e0', western: '#c8843c' };
        for (const l of LE.LOOKS.filter((l) => !q || l.includes(q))) g.append(h('button', { class: 'tile', title: 'Apply live (click) · right-click to add look("' + l + '") to the script', onclick: () => { LE.store.set('look.preset', l); LE.toast('Look: ' + l + ' (live override)'); }, oncontextmenu: (e) => { e.preventDefault(); this.insert('look "' + l + '"'); } },
          h('div', { class: 'sw', style: 'background:linear-gradient(135deg, #000, ' + tint[l] + ' 60%, #fff)' }), h('div', { class: 'nm' }, l)));
        b.append(g);
      } else {
        b.append(h('div', { class: 'dim', style: 'padding:8px 8px 0' }, 'Endless sounds'));
        const g = h('div', { class: 'grid' });
        for (const s of SOUNDS.filter((s) => !q || s.includes(q))) g.append(h('button', { class: 'tile', onclick: () => this.insert('sound "' + s + '", at: vec(0, 1, 0)') }, h('div', { class: 'sw', style: 'display:grid;place-items:center;font-size:20px;background:var(--bg2)' }, '♪'), h('div', { class: 'nm' }, s)));
        b.append(g);
        b.append(h('div', { class: 'dim', style: 'padding:8px 8px 0' }, 'One-off sounds'));
        const g2 = h('div', { class: 'grid' });
        for (const s of ONESHOTS.filter((s) => !q || s.includes(q))) g2.append(h('button', { class: 'tile', onclick: () => this.insert('sound "' + s + '", variant: "wood", at: vec(0, 0, 0)') }, h('div', { class: 'sw', style: 'display:grid;place-items:center;font-size:20px;background:var(--bg2)' }, '♫'), h('div', { class: 'nm' }, s)));
        b.append(g2);
      }
    },
  };

  // ============================================================ Timeline
  P.timeline = {
    pxPerS: 40,
    init() {
      const root = el('timeline');
      this.info = h('span', { class: 'dim mono' });
      this.zoom = h('input', { type: 'range', min: 8, max: 160, value: 40, style: 'width:120px', 'aria-label': 'Zoom' });
      this.zoom.oninput = () => { this.pxPerS = +this.zoom.value; this.draw(); };
      root.append(h('div', { class: 'ptools' },
        h('button', { title: 'Play the film (Movie mode)', onclick: () => LE.run('movie') }, '▶ Film'),
        h('button', { title: 'Back to the start', onclick: () => { E.setOption('filmtime', 0); } }, '⏮'),
        h('button', { title: 'Pause the world', onclick: () => LE.run('pause') }, '❚❚'),
        this.info, h('span', { class: 'spacer' }), h('span', { class: 'dim' }, 'Zoom'), this.zoom));
      this.body = h('div', { class: 'tl' });
      root.append(this.body);
      LE.emitSceneInfo = ((prev) => (info) => { prev && prev(info); this.sceneInfo = info; if (visible('timeline')) this.draw(); })(LE.emitSceneInfo);
      setInterval(() => this.tick(), 100);
    },
    show() { this.draw(); },
    draw() {
      const b = this.body;
      b.innerHTML = '';
      const info = this.sceneInfo;
      if (!info || !info.shotList || !info.shotList.length) { b.append(h('div', { class: 'empty' }, 'This scene has no shots. Add one with shot(...) or orbit(...) to get a film.')); return; }
      const len = info.length || 1, px = this.pxPerS;
      const ruler = h('div', { class: 'ruler', style: 'width:' + len * px + 'px', title: 'Click to jump (Movie mode)' });
      const step = px < 20 ? 10 : px < 50 ? 5 : 1;
      for (let t = 0; t <= len; t += step) ruler.append(h('span', { style: 'left:' + t * px + 'px' }, t + 's'));
      ruler.onclick = (e) => { const r = ruler.getBoundingClientRect(); const t = (e.clientX - r.left) / px; if (!E.movie) LE.run('movie'); E.setOption('filmtime', t); };
      b.append(ruler);
      const lane = h('div', { class: 'lane', style: 'width:' + len * px + 'px' });
      let t0 = 0;
      info.shotList.forEach((s, i) => {
        lane.append(h('div', { class: 'clip' + (s.orbit ? ' alt' : ''), style: 'left:' + t0 * px + 'px;width:' + Math.max(4, s.duration * px - 2) + 'px', title: (s.name || 'shot ' + (i + 1)) + ' · ' + s.duration.toFixed(1) + ' s' + (s.orbit ? ' · orbit' : s.moves ? ' · moves' : ' · holds') + (s.look ? ' · look ' + s.look : ''), onclick: ((t) => () => { if (!E.movie) LE.run('movie'); E.setOption('filmtime', t + 0.01); })(t0) }, (s.name || 'shot ' + (i + 1)) + (s.look ? ' · ' + s.look : '')));
        t0 += s.duration;
      });
      b.append(h('div', { class: 'track' }, h('div', { class: 'tn' }, 'Camera'), lane));
      const looks = h('div', { class: 'lane', style: 'width:' + len * px + 'px' });
      t0 = 0;
      info.shotList.forEach((s) => { if (s.fadeIn) looks.append(h('div', { class: 'clip alt', style: 'left:' + t0 * px + 'px;width:' + s.fadeIn * px + 'px', title: 'fade in' }, 'fade')); if (s.fadeOut) looks.append(h('div', { class: 'clip alt', style: 'left:' + (t0 + s.duration - s.fadeOut) * px + 'px;width:' + s.fadeOut * px + 'px', title: 'fade out' }, 'fade')); t0 += s.duration; });
      b.append(h('div', { class: 'track' }, h('div', { class: 'tn' }, 'Fades'), looks));
      b.append(h('div', { class: 'track' }, h('div', { class: 'tn' }, 'Events ', h('span', { class: 'badge nw' }, 'Not wired')), h('div', { class: 'lane dim', style: 'font-size:11px;padding-top:3px' }, 'Lines, timers and `wait` on the timeline come next.')));
      this.head = h('div', { class: 'head' });
      b.style.position = 'relative';
      b.append(this.head);
    },
    tick() {
      if (!this.head || !visible('timeline') || !E.M) return;
      const s = E.stats();
      this.info.textContent = (s.movie ? 'film ' + (s.film || 0).toFixed(2) + ' s' : 'game mode') + ' · world ' + (s.time || 0).toFixed(1) + ' s';
      this.head.style.left = 8 + 110 + (s.film || 0) * this.pxPerS + 'px';
      this.head.style.display = s.movie ? 'block' : 'none';
    },
  };

  // ============================================================ Profiler
  P.profiler = {
    runs: [],
    init() {
      const root = el('profiler');
      this.canvas = h('canvas', { width: 720, height: 150, 'aria-label': 'Frame times' });
      this.kv = h('div', { class: 'kv' });
      this.table = h('div');
      root.append(h('div', { class: 'ptools' }, h('button', { class: 'primary', onclick: () => LE.run('benchmark') }, '⏱ Benchmark this scene'), h('span', { class: 'dim' }, 'Frame times, last 240 frames · lines at 16.7 ms (60 fps) and 33 ms (30 fps)')), h('div', { class: 'prof' }, this.canvas, this.kv, this.table));
      setInterval(() => this.tick(), 250);
    },
    show() { this.tick(); this.drawRuns(); },
    tick() {
      if (!visible('profiler') || !E.M) return;
      const s = E.stats();
      const cells = [['fps', (s.fps || 0).toFixed(0)], ['GPU ms', (s.gpu || 0).toFixed(1)], ['CPU ms', (s.cpu || 0).toFixed(1)], ['resolution', Math.round((s.scale || 1) * 100) + '%'], ['pixel ratio', (s.ratio || 1).toFixed(2) + 'x'], ['draw calls', s.draws ?? '—'], ['things', s.things ?? '—'], ['lights', s.lights ?? '—'], ['frames in flight', s.inflight ?? '—'], ['world time', (s.time || 0).toFixed(1) + ' s']];
      this.kv.innerHTML = '';
      for (const [k, v] of cells) this.kv.append(h('div', {}, h('b', {}, v), h('span', {}, k)));
      const g = E.perfGraph();
      const c = this.canvas, dpr = devicePixelRatio || 1;
      const W = c.clientWidth * dpr, H = c.clientHeight * dpr;
      if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
      const x = c.getContext('2d');
      x.clearRect(0, 0, W, H);
      const max = Math.max(50, ...g) * 1.1;
      const y = (ms) => H - (ms / max) * H;
      x.strokeStyle = '#4cc38a88'; x.setLineDash([4 * dpr, 4 * dpr]); x.beginPath(); x.moveTo(0, y(16.7)); x.lineTo(W, y(16.7)); x.stroke();
      x.strokeStyle = '#f0b54488'; x.beginPath(); x.moveTo(0, y(33.3)); x.lineTo(W, y(33.3)); x.stroke(); x.setLineDash([]);
      const bw = W / g.length;
      g.forEach((ms, i) => { x.fillStyle = ms > 33.4 ? '#ef5d5d' : ms > 16.8 ? '#f0b544' : '#4c9dff'; x.fillRect(i * bw, y(ms), Math.max(1, bw - dpr), H - y(ms)); });
      x.fillStyle = '#8b94a3'; x.font = 11 * dpr + 'px ui-monospace, monospace';
      x.fillText(max.toFixed(0) + ' ms', 4 * dpr, 12 * dpr);
    },
    addRun(r) { this.runs.unshift(r); this.drawRuns(); },
    drawRuns() {
      this.table.innerHTML = '';
      if (!this.runs.length) { this.table.append(h('div', { class: 'dim' }, 'No benchmark runs yet in this session.')); return; }
      const cols = [['scene', 'Scene'], ['quality', 'Quality'], ['fps', 'fps'], ['p50Ms', 'median ms'], ['p99Ms', '99% ms'], ['low1Fps', '1% low'], ['hitches', 'hitches'], ['gpuMs', 'GPU ms'], ['cpuMs', 'CPU ms'], ['scaleAvg', 'res'], ['loadMs', 'load ms']];
      const t = h('table', { class: 'data' }, h('tr', {}, cols.map(([, l]) => h('th', {}, l))));
      for (const r of this.runs) t.append(h('tr', {}, cols.map(([k]) => h('td', {}, typeof r[k] === 'number' ? (k === 'scaleAvg' ? Math.round(r[k] * 100) + '%' : r[k].toFixed(k === 'hitches' || k === 'loadMs' ? 0 : 1)) : r[k] ?? ''))));
      this.table.append(t, h('div', { style: 'margin-top:6px' }, h('button', { onclick: () => LE.download('loop3d-bench.json', JSON.stringify(this.runs, null, 1), 'application/json') }, 'Save runs (.json)')));
    },
  };

  LE.initPanels = () => { for (const p of Object.values(P)) p.init && p.init(); };
})();
