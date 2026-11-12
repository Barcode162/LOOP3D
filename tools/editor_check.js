// Checks the editor in a real browser, on a phone screen and a computer screen:
// adds a box, taps it, drags it (its line is rewritten), undoes and redoes, opens it in
// the Studio (two keys, scrub, play, undo, Done), places a piece from the library,
// opens Logic, adds a shot in Film, a sound and music in Sound, and reloads (nothing
// lost). Pictures go to <out dir>; it fails if any step goes wrong.
//   (cd site && python3 -m http.server 8123 &)
//   xvfb-run -a node tools/editor_check.js http://localhost:8123 <out dir>
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
  check(Math.abs(mid[0] - (ends[0][0] + ends[1][0]) / 2) < 1e-3 && Math.abs(mid[2] - (ends[0][2] + ends[1][2]) / 2) < 1e-3, name + ': at 1 s the box is half way between its two keys');
  await p.screenshot({ path: `${OUT}/${name}_studio.png` });
  await tool('Play');
  // the world's clock runs slower than a real one on a drawn-by-software test machine: wait for the end, not for a time
  await p.waitForFunction(() => !LE.studio.playing, null, { timeout: 60000 }).catch(() => {});
  check(await p.evaluate(() => !LE.studio.playing && Math.abs(LE.studio.moment - 2) < 0.05), name + ': Play runs to the last key and stops there');
  await p.click('#undoBtn'); await p.waitForTimeout(600);
  check((await lines('^  key')).length === 1 && await p.evaluate(() => LE.engine.clips().find((c) => c.target === LE.studio.index).channels[0].keys.length === 1), name + ': undo takes the second key back, in the script and in the engine');
  await p.click('#redoBtn'); await p.waitForTimeout(600);
  check((await lines('^  key')).length === 2, name + ': redo brings it back');
  await tool('Starts again'); await p.waitForTimeout(500);
  check((await lines('^clip box_1, loop: yes')).length === 1, name + ': Starts again writes loop: yes');
  await tool('Done'); await p.waitForTimeout(2500);
  check(await p.evaluate(() => !LE.studio.open && LE.logic.problems.length === 0), name + ': Done goes back to the world, and the script has no problems');
  const x0 = await p.evaluate(() => LE.engine.thing(LE.view.sel.index).position[0]);
  await p.waitForTimeout(500);
  const x1 = await p.evaluate(() => LE.engine.thing(LE.view.sel.index).position[0]);
  check(Math.abs(x1 - x0) > 1e-3, name + ': back in the world the box moves by its keys');
  // the library: a tap on one of its pieces brings it in (its use line, once) and places it
  await p.evaluate(() => LE.view.select(null)); await p.waitForTimeout(400);
  const things0 = await p.evaluate(() => LE.spaces.refreshInfo().things.length);
  await tap('plinth'); await p.waitForTimeout(2500);
  check((await lines('^use "plinth"')).length === 1 && (await lines('^plinth\\(')).length === 1, name + ': a library piece adds its use line and itself (' + (await lines('^plinth'))[0] + ')');
  check(await p.evaluate((n) => LE.spaces.refreshInfo().things.length > n, things0), name + ': the plinth stands in the world');
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
  check(await p.evaluate(() => LE.logic.problems.length === 0), name + ': the edited script has no problems');
  await p.reload();
  await ready();
  await p.waitForTimeout(1500);
  check((await lines('^(let box_1 = box|clip box_1|  key|shot|sound|track)')).length === 8, name + ': a reload keeps every change');
  check(await p.evaluate(() => LE.history.undo.length >= 4), name + ': and undo still knows them');
  check(!errs.length, name + ': no page errors' + (errs.length ? '\n' + errs.join('\n') : ''));
  await ctx.close();
}

(async () => {
  const browser = await chromium.launch({ headless: false, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] });
  await run(browser, 'phone', { width: 430, height: 932 }, true);
  await run(browser, 'computer', { width: 1440, height: 900 }, false);
  await browser.close();
  console.log(failed ? failed + ' failed' : 'the editor works');
  process.exit(failed ? 1 : 0);
})();
