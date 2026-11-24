// Checks the editor in a real browser, on a phone screen and a computer screen:
// adds a box, taps it, drags it (its line is rewritten), undoes and redoes, opens it in
// the Studio (two keys, scrub, play, undo, Done), places a piece from the library,
// opens Logic, adds a shot in Film, a sound and music in Sound, and reloads (nothing
// lost). Pictures go to <out dir>; it fails if any step goes wrong.
//   (cd site && python3 -m http.server 8123 &)
//   xvfb-run -a node tools/editor_check.js http://localhost:8123 <out dir> [phone | computer]
// Run it on a virtual display: headless browsers lose WebGPU (docs/TUNING.md §8).
const { chromium } = require('playwright');
const fs = require('fs');
const BASE = process.argv[2] || 'http://localhost:8123', OUT = process.argv[3] || 'out';
fs.mkdirSync(OUT, { recursive: true });
let failed = 0;
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) failed++; };

async function run(browser, name, viewport, touch) {
  const ctx = await browser.newContext({ viewport, hasTouch: touch, isMobile: touch, deviceScaleFactor: 1 });
  const p = await ctx.newPage();
  const errs = [];
  p.on('pageerror', (e) => errs.push(String(e.stack || e)));
  p.on('console', (m) => { if (m.type() === 'error' && !m.text().includes('Failed to load')) errs.push(m.text().slice(0, 300)); });
  const ready = () => p.waitForFunction(() => window.LE && LE.engine.ready && document.getElementById('vpNote').style.display === 'none', null, { timeout: 120000 });
  await p.goto(BASE + '/editor.html#first_room');
  // a first visit reloads itself once, to share memory across the cores (web/isolate.js): wait for it
  await p.waitForFunction(() => window.crossOriginIsolated, null, { timeout: 30000 }).catch(() => {});
  await p.waitForLoadState('load');
  await p.evaluate(() => { try { localStorage.clear(); indexedDB.deleteDatabase('loop3d-editor'); } catch (e) { /* fresh anyway */ } });
  await p.reload();
  await ready();
  await p.waitForTimeout(1500);
  const lines = (re) => p.evaluate((src) => LE.history.text.split('\n').filter((l) => new RegExp(src).test(l)), re);
  const tap = (label) => p.evaluate((l) => { const b = [...document.querySelectorAll('button')].find((x) => x.textContent.trim() === l && x.offsetParent); if (!b) throw new Error('no button ' + l); b.click(); }, label);
  const spot = () => p.evaluate(() => {
    const t = LE.engine.thing(LE.view.sel.index), v = LE.engine.view(), m = v.viewProj, q = t.position;
    const x = m[0] * q[0] + m[4] * q[1] + m[8] * q[2] + m[12], y = m[1] * q[0] + m[5] * q[1] + m[9] * q[2] + m[13], w = m[3] * q[0] + m[7] * q[1] + m[11] * q[2] + m[15];
    const r = document.getElementById('view').getBoundingClientRect();
    return { x: r.left + (x / w * 0.5 + 0.5) * r.width, y: r.top + (1 - (y / w * 0.5 + 0.5)) * r.height };
  });

  await tap('Box');
  // wait for the box itself (a fixed 2.5 s failed one run in two on a busy machine)
  await p.waitForFunction(() => window.LE && LE.view.sel && LE.history.text.split('\n').some((l) => /^box/.test(l)), null, { timeout: 20000 }).catch(() => {});
  await p.waitForTimeout(500);
  const box = await lines('^box');
  check(box.length === 1, name + ': Box adds a line (' + box[0] + ')');
  check(await p.evaluate(() => !!LE.view.sel), name + ': the new box is picked');
  const s0 = await spot();
  await p.evaluate(() => LE.view.select(null));
  if (touch) await p.touchscreen.tap(s0.x, s0.y); else await p.mouse.click(s0.x, s0.y);
  await p.waitForTimeout(800);
  check(await p.evaluate(() => LE.view.sel && LE.view.sel.line === 7), name + ': a tap on the box picks it (line 7)');
  await p.screenshot({ path: `${OUT}/${name}_picked.png` });
  const s1 = await spot();
  await p.mouse.move(s1.x, s1.y); await p.mouse.down();
  for (let i = 1; i <= 8; i++) { await p.mouse.move(s1.x + i * 10, s1.y - i * 3); await p.waitForTimeout(30); }
  await p.mouse.up();
  await p.waitForTimeout(800);
  const moved = await lines('^box');
  check(moved[0] !== box[0] && /at: vec\(/.test(moved[0]), name + ': dragging rewrites its line (' + moved[0] + ')');
  await p.click('#undoBtn'); await p.waitForTimeout(600);
  check((await lines('^box'))[0] === box[0], name + ': undo puts it back');
  await p.click('#redoBtn'); await p.waitForTimeout(600);
  check((await lines('^box'))[0] === moved[0], name + ': redo moves it again');
  // the Studio: open the box, give it two keys at two moments, scrub between them, play, undo, Done
  const tool = (label) => p.evaluate((l) => { const b = [...document.querySelectorAll('#sheetBody button')].find((x) => x.textContent.trim() === l && x.offsetParent); if (!b) throw new Error('no button ' + l + ' in the sheet'); b.click(); }, label);
  const dragThing = async (dx, dy) => {
    const s = await spot();
    await p.mouse.move(s.x, s.y); await p.mouse.down();
    for (let i = 1; i <= 8; i++) { await p.mouse.move(s.x + dx * i / 8, s.y + dy * i / 8); await p.waitForTimeout(30); }
    await p.mouse.up(); await p.waitForTimeout(700);
  };
  await tool('Open'); await p.waitForTimeout(600);
  check(await p.evaluate(() => LE.studio.open && !!document.querySelector('.kt')), name + ': Open shows the Studio and its strip of time');
  await dragThing(-60, 10);
  const k1 = await lines('^  key');
  check((await lines('^let box_1 = box\\(')).length === 1 && (await lines('^clip box_1')).length === 1 && k1.length === 1 && /^  key 0 s, at: vec\(/.test(k1[0]),
    name + ': a move in the Studio names the box and keeps a key at 0 s (' + k1.join(' | ') + ')');
  const strip = await p.evaluate(() => { const r = document.querySelector('.kt-touch').getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width }; });
  await p.mouse.click(strip.x + strip.w * 0.5, strip.y + 10); await p.waitForTimeout(400);
  check(await p.evaluate(() => Math.abs(LE.studio.moment - 2) < 1e-6), name + ': a press on the strip moves the white line to 2 s');
  await dragThing(120, -20);
  const k2 = await lines('^  key');
  check(k2.length === 2 && /^  key 2 s, at: vec\(/.test(k2[1]), name + ': a move at another moment is a second key (' + k2.join(' | ') + ')');
  const ends = await p.evaluate(() => LE.engine.clips().find((c) => c.target === LE.studio.index).channels[0].keys.map((k) => k.v));
  await p.mouse.click(strip.x + strip.w * 0.25, strip.y + 10); await p.waitForTimeout(400);
  const mid = await p.evaluate(() => LE.engine.thing(LE.studio.index).position);
  // between two smooth keys the box is u*u*(3 - 2u) of the way at the share u of the time (half way at half time);
  // the click lands on a frame near 1 s, so the share is taken from the white line's own moment
  const share = await p.evaluate(() => { const u = LE.studio.moment / 2; return u * u * (3 - 2 * u); });
  check(share > 0.4 && share < 0.6 && [0, 2].every((k) => Math.abs(mid[k] - (ends[0][k] + (ends[1][k] - ends[0][k]) * share)) < 1e-3), name + ': near 1 s the box is half way between its two keys');
  await p.screenshot({ path: `${OUT}/${name}_studio.png` });
  await tool('Play');
  // the world's clock runs slower than a real one on a drawn-by-software test machine: wait for the end, not for a time
  await p.waitForFunction(() => !LE.studio.playing, null, { timeout: 60000 }).catch(() => {});
  check(await p.evaluate(() => !LE.studio.playing && Math.abs(LE.studio.moment - 2) < 0.05), name + ': Play runs to the last key and stops there');
  await p.click('#undoBtn'); await p.waitForTimeout(600);
  check((await lines('^  key')).length === 1 && await p.evaluate(() => LE.engine.clips().find((c) => c.target === LE.studio.index).channels[0].keys.length === 1), name + ': undo takes the second key back, in the script and in the engine');
  await p.click('#redoBtn'); await p.waitForTimeout(600);
  check((await lines('^  key')).length === 2, name + ': redo brings it back');
  // the curve: the second key arrives held, then free with a handle pulled, then smooth again
  await p.mouse.click(strip.x + strip.w * 0.5, strip.y + 26 + 18); await p.waitForTimeout(500);
  check(await p.evaluate(() => LE.studio.key && Math.abs(LE.studio.key.t - 2) < 1e-6 && !!document.querySelector('canvas.curve')), name + ': a tap on a diamond picks the key and shows its curve');
  await tool('Held'); await p.waitForTimeout(500);
  check((await lines('^  key 2 s, at: vec\\(.*ease: "hold"')).length === 1, name + ': Held writes ease: "hold"');
  await p.evaluate(() => { const r = document.querySelector('.kt-touch').getBoundingClientRect(); window.scrollTo(0, 0); document.getElementById('sheetBody').scrollTop = 0; return r; });
  const strip2 = await p.evaluate(() => { const r = document.querySelector('.kt-touch').getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width }; });
  await p.mouse.click(strip2.x + strip2.w * 0.375, strip2.y + 10); await p.waitForTimeout(400);
  const heldAt = await p.evaluate(() => LE.engine.thing(LE.studio.index).position);
  check(Math.abs(heldAt[0] - ends[0][0]) < 1e-4 && Math.abs(heldAt[2] - ends[0][2]) < 1e-4, name + ': a held key keeps the box still until its moment');
  await p.mouse.click(strip2.x + strip2.w * 0.5, strip2.y + 26 + 18); await p.waitForTimeout(500);
  await tool('Free'); await p.waitForTimeout(600);
  // the curve is drawn on the page's next frame, which a slow test machine may take a while to reach
  await p.waitForFunction(() => LE.curves.shown && LE.curves.shown.handles.some((x) => x.side === 'in'), null, { timeout: 20000 }).catch(() => {});
  check((await lines('^  key 2 s, at: vec\\(.*, in: vec\\(.*, out: vec\\(')).length === 1, name + ': Free gives the key its own slopes (in and out)');
  const handle = await p.evaluate(() => {
    const c = document.querySelector('canvas.curve');
    c.scrollIntoView({ block: 'center' });
    const r = c.getBoundingClientRect(), hd = LE.curves.shown.handles.find((x) => x.side === 'in');
    return hd ? { x: r.left + hd.x, y: r.top + hd.y } : null;
  });
  check(!!handle, name + ': a free key shows its handle');
  if (handle) {
    const before = (await lines('^  key 2 s'))[0];
    await p.mouse.move(handle.x, handle.y); await p.mouse.down();
    for (let i = 1; i <= 6; i++) { await p.mouse.move(handle.x, handle.y + i * 6); await p.waitForTimeout(30); }
    await p.mouse.up(); await p.waitForTimeout(600);
    const after = (await lines('^  key 2 s'))[0];
    check(after !== before && /in: vec\(/.test(after), name + ': pulling a handle reshapes the curve (' + after.trim() + ')');
    await p.screenshot({ path: `${OUT}/${name}_curve.png` });
  }
  await tool('Smooth'); await p.waitForTimeout(500);
  check((await lines('^  key 2 s, at: vec\\([^)]*\\)$')).length === 1, name + ': Smooth takes the slopes away again');
  await p.evaluate(() => { document.getElementById('sheetBody').scrollTop = 0; });
  await tool('Starts again'); await p.waitForTimeout(500);
  check((await lines('^clip box_1, loop: yes')).length === 1, name + ': Starts again writes loop: yes');
  await tool('Done'); await p.waitForTimeout(2500);
  check(await p.evaluate(() => !LE.studio.open && LE.logic.problems.length === 0), name + ': Done goes back to the world, and the script has no problems');
  const x0 = await p.evaluate(() => LE.engine.thing(LE.view.sel.index).position[0]);
  const went = await p.waitForFunction((x) => Math.abs(LE.engine.thing(LE.view.sel.index).position[0] - x) > 1e-3, x0, { timeout: 20000 }).then(() => true, () => false);
  check(went, name + ': back in the world the box moves by its keys');
  // the Shape bench: open the box, drag one of its corners, let go (a pull line goes under the box), undo, redo
  const boxHi = () => p.evaluate(() => {
    const at = LE.history.text.split('\n').findIndex((l) => /^let box_1 = box/.test(l)) + 1 + (LE.engine.lineOffset || 0);
    const i = LE.engine.sceneInfo().things.findIndex((t) => t.line === at);
    const t = LE.engine.thing(i);
    return t ? t.hi.concat(t.lo) : null;
  });
  const hi0 = await boxHi();
  await tool('Open'); await p.waitForTimeout(600);
  await tool('Shape'); await p.waitForTimeout(800);
  await p.waitForFunction(() => LE.studio.dots && LE.studio.dots.length === 8, null, { timeout: 20000 }).catch(() => {});
  check(await p.evaluate(() => LE.studio.bench === 'shape' && LE.studio.dots.length === 8), name + ': the Shape bench shows the box\'s 8 points as dots');
  const dot = await p.evaluate(() => {
    const r = document.getElementById('view').getBoundingClientRect(), d = LE.studio.dots.slice().sort((a, b) => a.y - b.y)[0];
    return { x: r.left + d.x, y: r.top + d.y, name: d.name };
  });
  await p.mouse.move(dot.x, dot.y); await p.mouse.down();
  for (let i = 1; i <= 8; i++) { await p.mouse.move(dot.x + i * 8, dot.y - i * 4); await p.waitForTimeout(40); }
  const live = await boxHi();
  check(live && hi0 && live.some((v, k) => Math.abs(v - hi0[k]) > 1e-3), name + ': while the dot is held the shape follows it');
  await p.mouse.up(); await p.waitForTimeout(700);
  const pull = await lines('^  pull point: ');
  check(pull.length === 1 && /^  pull point: \d+, by: vec\(/.test(pull[0]) && (await lines('^let box_1 = box\\(')).length === 1, name + ': letting go writes one pull line under the box (' + pull[0] + ')');
  const hi1 = await boxHi();
  check(hi1 && hi0 && hi1.some((v, k) => Math.abs(v - hi0[k]) > 1e-3), name + ': the box is pulled out');
  await p.screenshot({ path: `${OUT}/${name}_shape.png` });
  await p.click('#undoBtn'); await p.waitForTimeout(2500);
  const hiU = await boxHi();
  check((await lines('^  pull point: ')).length === 0 && hiU && hiU.every((v, k) => Math.abs(v - hi0[k]) < 1e-3), name + ': one undo takes the pull back, in the script and in the shape');
  await p.click('#redoBtn'); await p.waitForTimeout(2500);
  const hiR = await boxHi();
  check((await lines('^  pull point: ')).length === 1 && hiR && hiR.every((v, k) => Math.abs(v - hi1[k]) < 1e-3), name + ': redo pulls it again');
  // the Faces tool (goal 19a): an arrow on each face you can see; drag one out and an extrude line goes under the box
  await tool('Faces'); await p.waitForTimeout(800);
  check(await p.evaluate(() => LE.studio.tool === 'faces' && LE.studio.knobs.length >= 1 && LE.studio.knobs.length <= 3), name + ': the Faces tool shows an arrow on each face that looks towards you (' + (await p.evaluate(() => LE.studio.knobs.length)) + ')');
  const knob = await p.evaluate(() => {
    const r = document.getElementById('view').getBoundingClientRect(), k = LE.studio.knobs[0];
    const dx = k.x - k.from[0], dy = k.y - k.from[1], l = Math.hypot(dx, dy) || 1;
    return { x: r.left + k.x, y: r.top + k.y, dx: dx / l, dy: dy / l };
  });
  await p.mouse.move(knob.x, knob.y); await p.mouse.down();
  for (let i = 1; i <= 8; i++) { await p.mouse.move(knob.x + knob.dx * i * 8, knob.y + knob.dy * i * 8); await p.waitForTimeout(40); }
  const grown = await p.evaluate(() => LE.studio.shape && LE.studio.shape.points.length);
  check(grown === 12, name + ': while the arrow is held the face grows a block (12 points: ' + grown + ')');
  await p.mouse.up(); await p.waitForTimeout(700);
  const ext = await lines('^  extrude face: ');
  check(ext.length === 1 && /^  extrude face: \d+, by: \d/.test(ext[0]), name + ': letting go writes one extrude line under the box, outwards (' + ext[0] + ')');
  await p.screenshot({ path: `${OUT}/${name}_extrude.png` });
  await p.click('#undoBtn'); await p.waitForTimeout(2500);
  check((await lines('^  extrude face: ')).length === 0, name + ': one undo takes the extrude back');
  await p.click('#redoBtn'); await p.waitForTimeout(2500);
  check((await lines('^  extrude face: ')).length === 1, name + ': redo extrudes it again');
  const hiE = await boxHi();
  await tool('Done'); await p.waitForTimeout(2500);
  check(await p.evaluate(() => !LE.studio.open && LE.logic.problems.length === 0), name + ': Done after a pull and an extrude leaves no problems in the script');
  // the library: a tap on one of its pieces brings it in (its use line, once) and places it
  await p.evaluate(() => LE.view.select(null)); await p.waitForTimeout(400);
  const things0 = await p.evaluate(() => LE.spaces.refreshInfo().things.length);
  await tap('plinth'); await p.waitForTimeout(2500);
  check((await lines('^use "plinth"')).length === 1 && (await lines('^plinth\\(')).length === 1, name + ': a library piece adds its use line and itself (' + (await lines('^plinth'))[0] + ')');
  check(await p.evaluate((n) => LE.spaces.refreshInfo().things.length > n, things0), name + ': the plinth stands in the world');
  // the Scripts shelf: hold walk controls, drag it onto the view, let go: it lands where the finger left it
  check(await p.evaluate(() => (document.querySelector('.shelf-head b') || {}).textContent === 'Scripts'), name + ': the library opens on its Scripts shelf');
  const held = await p.evaluate(() => { const b = [...document.querySelectorAll('.tools.library button')].find((x) => x.textContent.trim() === 'walk controls'); b.scrollIntoView({ block: 'center' }); const r = b.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  const onView = await p.evaluate(() => { const r = document.getElementById('view').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height * 0.3 }; });
  await p.mouse.move(held.x, held.y); await p.mouse.down(); await p.waitForTimeout(450);
  for (let i = 1; i <= 10; i++) { await p.mouse.move(held.x + (onView.x - held.x) * i / 10, held.y + (onView.y - held.y) * i / 10); await p.waitForTimeout(30); }
  check(await p.evaluate(() => !!document.querySelector('.lib-ghost.over')), name + ': the held script follows the finger over the view');
  await p.screenshot({ path: `${OUT}/${name}_library_drag.png` });
  await p.mouse.up(); await p.waitForTimeout(2500);
  check((await lines('^use "walk_controls"')).length === 1 && (await lines('^walk_controls\\(vec\\(')).length === 1, name + ': dropping it adds its use line and itself (' + (await lines('^walk_controls'))[0] + ')');
  // a lamp: dragging its marker moves its light at once (the light and its shadows follow the finger), and its line on letting go
  await tap('Lamp');
  // wait for the lamp and its marker on screen (a fixed wait failed one run in two on a busy machine)
  await p.waitForFunction(() => LE.history.text.split('\n').some((l) => /^light at: /.test(l)) && LE.view.extra.some((x) => x.kind === 'lamp' && x._p), null, { timeout: 20000 }).catch(() => {});
  await p.waitForTimeout(500);
  const lamp0 = await lines('^light at: ');
  const mk = await p.evaluate(() => { const m = LE.view.extra.find((x) => x.kind === 'lamp' && x._p); if (!m) return null; const r = document.getElementById('view').getBoundingClientRect(); return { x: r.left + m._p[0], y: r.top + m._p[1], at: m.at.slice() }; });
  check(!!mk && lamp0.length === 1, name + ': the Lamp button adds a lamp with a marker');
  if (mk) {
    const lampAt = () => p.evaluate((at) => { const L = (LE.engine.sceneInfo().lights || []).map((l) => l.position); let best = null, bd = Infinity; for (const q of L) { const d = Math.hypot(q[0] - at[0], q[2] - at[2]); if (d < bd) { bd = d; best = q; } } return best; }, mk.at);
    const before = await lampAt();
    await p.mouse.move(mk.x, mk.y); await p.mouse.down();
    for (let i = 1; i <= 8; i++) { await p.mouse.move(mk.x + i * 6, mk.y + i * 2); await p.waitForTimeout(40); }
    const during = await p.evaluate(() => (LE.engine.sceneInfo().lights || []).map((l) => l.position));
    check(during.some((q) => Math.hypot(q[0] - before[0], q[2] - before[2]) > 0.02 && Math.abs(q[1] - before[1]) < 1e-3), name + ': while its marker is dragged the lamp itself moves');
    await p.mouse.up(); await p.waitForTimeout(2000);
    const lamp1 = await lines('^light at: ');
    check(lamp1.length === 1 && lamp1[0] !== lamp0[0], name + ': letting go rewrites its line (' + lamp1[0] + ')');
  }
  await p.click('#spaces [data-space=logic]'); await p.waitForTimeout(800);
  check(await p.evaluate(() => document.querySelector('.code textarea').value === LE.history.text), name + ': Logic shows the script');
  await p.screenshot({ path: `${OUT}/${name}_logic.png` });
  await p.click('#spaces [data-space=film]'); await p.waitForTimeout(500);
  await tap('Shot from here'); await p.waitForTimeout(2500);
  check((await lines('^shot')).length === 1, name + ': Shot from here adds a shot');
  check(await p.evaluate(() => LE.spaces.info && LE.spaces.info.shotList.length === 1), name + ': the engine has the shot');
  await p.screenshot({ path: `${OUT}/${name}_film.png` });
  await p.click('#spaces [data-space=sound]'); await p.waitForTimeout(500);
  await tap('fire'); await p.waitForTimeout(2500);
  check((await lines('^sound "fire"')).length === 1, name + ': a sound is added');
  check(await p.evaluate(() => LE.view.extra.some((m) => m.kind === 'sound')), name + ': the sound shows in the world');
  // music: a kick on the first beat and a bass note, written as tracks, playing in time
  await p.evaluate(() => { document.querySelector('.seqrow .cell[data-step="0"]').click(); });
  await p.waitForTimeout(400);
  await p.evaluate(() => { const rows = document.querySelectorAll('.seq')[1].querySelectorAll('.seqrow'); rows[rows.length - 1].querySelector('.cell[data-step="0"]').click(); });
  await p.waitForTimeout(1500);
  const tracks = await lines('^track');
  check(tracks.length === 2 && /"kick", "x\.{15}"/.test(tracks[0]) && /"bass", "C2( \.){15}"/.test(tracks[1]), name + ': the music grid writes tracks (' + tracks.join(' | ') + ')');
  check(await p.evaluate(() => LE.engine.musicStep() > 0), name + ': the music plays in time with the world');
  await p.screenshot({ path: `${OUT}/${name}_sound.png` });
  const left = await p.evaluate(() => LE.logic.problems.map((x) => (x.text || x.message || JSON.stringify(x)) + ' @' + x.line));
  check(left.length === 0, name + ': the edited script has no problems' + (left.length ? ' (' + left.join(' | ') + ')\n' + await p.evaluate(() => LE.history.text) : ''));
  await p.reload();
  await ready();
  await p.waitForTimeout(1500);
  check((await lines('^(let box_1 = box|clip box_1|  key|shot|sound|track)')).length === 8, name + ': a reload keeps every change');
  check(await p.evaluate(() => LE.history.undo.length >= 4), name + ': and undo still knows them');
  const hiAfter = await boxHi();
  check((await lines('^  pull point: ')).length === 1 && hiAfter && hiE && hiE.every((v, k) => Math.abs(v - hiAfter[k]) < 1e-3), name + ': after a reload the corner is still out, and the face');
  check((await lines('^  extrude face: ')).length === 1, name + ': and the extrude is still there');
  check(!errs.length, name + ': no page errors' + (errs.length ? '\n' + errs.join('\n') : ''));
  await ctx.close();
}

(async () => {
  const browser = await chromium.launch({ headless: false, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] });
  // a third word, phone or computer, runs only that one
  const only = process.argv[4];
  if (only !== 'computer') await run(browser, 'phone', { width: 430, height: 932 }, true);
  if (only !== 'phone') await run(browser, 'computer', { width: 1440, height: 900 }, false);
  await browser.close();
  console.log(failed ? failed + ' failed' : 'the editor works');
  process.exit(failed ? 1 : 0);
})();
