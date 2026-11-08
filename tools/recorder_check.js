// The live recorder in the real player (site/, after tools/build_site.sh),
// two ways: inside a frame with a pretend claude.ai database, as the
// recording page runs on the phone, and on its own, as the public player.
//   xvfb-run -a node tools/recorder_check.js
//   PAGE=build/recording/index.html xvfb-run -a node tools/recorder_check.js
//     frames the recording page as published (tools/recording_page.py),
//     wrapped the way claude.ai wraps a page
// It plays a scene, moves to another, and checks what each saved: the parts,
// their scenes, frames and load times, one core inside the frame, no editor.
const { chromium } = require('playwright');
const fs = require('fs'), http = require('http'), path = require('path');
const SITE = path.resolve(__dirname, '..', 'site');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.png': 'image/png', '.glb': 'model/gltf-binary', '.loop': 'text/plain' };
// the frame the recording page puts the player in
const PAGE = process.env.PAGE ? fs.readFileSync(process.env.PAGE, 'utf8') : null;
const FRAME = '<!doctype html><meta charset="utf-8"><body style="margin:0"><iframe id="f" src="' + (PAGE ? 'page.html' : 'index.html') + '?quality=0#courtyard" style="border:0;width:640px;height:360px"></iframe>';
const WRAP = (body) => '<!doctype html><html><head><meta charset=utf8><meta name=viewport content="width=device-width,initial-scale=1,viewport-fit=cover"><style>:root{color-scheme:light;box-sizing:border-box;padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}body{margin:0;padding:0;font:14px -apple-system,BlinkMacSystemFont,sans-serif;background:#fff;color:#000}img{max-width:100%}[hidden]:not([hidden=until-found i]){display:none!important}</style></head><body>' + body + '</body></html>';

let failed = 0, checks = 0;
const ok = (cond, what, got) => { checks++; if (!cond) { failed++; console.log('FAIL ' + what + (got !== undefined ? '\n  got: ' + JSON.stringify(got) : '')); } };

(async () => {
  if (!fs.existsSync(path.join(SITE, 'player.wasm'))) { console.log('build the site first: tools/build_site.sh'); process.exit(2); }
  const server = http.createServer((req, res) => {
    const url = decodeURIComponent(req.url.split('?')[0]);
    if (url === '/frame.html') { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(FRAME); return; }
    if (url === '/page.html' && PAGE) { res.writeHead(200, { 'Content-Type': 'text/html' }); res.end(WRAP(PAGE)); return; }
    // the page's models, as text (tools/recording_page.py)
    if (PAGE && url.startsWith('/scenes/models/') && url.endsWith('.glb.json')) {
      const f = path.join(path.dirname(path.resolve(process.env.PAGE)), 'models', path.basename(url));
      if (!fs.existsSync(f)) { res.writeHead(404); res.end(); return; }
      res.writeHead(200, { 'Content-Type': 'application/json' }); fs.createReadStream(f).pipe(res); return;
    }
    if (PAGE && url.endsWith('.glb')) { res.writeHead(404); res.end(); return; }  // claude.ai serves none
    const file = path.join(SITE, url.replace(/\/$/, '/index.html'));
    if (!file.startsWith(SITE) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  await new Promise((r) => server.listen(0, r));
  const base = `http://localhost:${server.address().port}/`;
  const exe = fs.readdirSync('/opt/pw-browsers').filter((d) => d.startsWith('chromium-'))[0];
  const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    executablePath: `/opt/pw-browsers/${exe}/chrome-linux64/chrome`,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'],
  });
  const errors = [];
  const watch = (page) => {
    page.on('pageerror', (e) => errors.push(String(e)));
    page.on('console', (m) => { if (m.type() === 'error' && !m.text().includes('Failed to load resource')) errors.push(m.text()); });
  };
  const until = async (frame, fn, what, ms = 120000) => {
    try { await frame.waitForFunction(fn, null, { timeout: ms }); return true; } catch (e) { ok(false, what + ' (timed out)'); return false; }
  };
  const settle = (ms) => new Promise((r) => setTimeout(r, ms));

  // ---- 1. framed, with a database: the recording page
  {
    const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
    watch(page);
    // claude.ai gives a framed page window.claude before its scripts; this one's
    // database keeps what is written, for the check to read from the top page
    await page.addInitScript(() => {
      if (window.self === window.top) return;
      const writes = (window.top.dbWrites = window.top.dbWrites || []);
      const db = { doc: (p) => ({ set: (body) => { writes.push({ path: p, body: JSON.parse(JSON.stringify(body)) }); return Promise.resolve(); } }) };
      window.claude = { use: (name) => Promise.resolve(name === 'db' ? db : null) };
    });
    await page.goto(base + 'frame.html');
    const frame = await (await page.waitForSelector('#f')).contentFrame();
    if (await until(frame, () => window.loopRecorder && window.Module && document.getElementById('note').style.display === 'none', 'the framed player starts')) {
      await settle(6000);
      await frame.evaluate(() => window.loopRecorder.cut());
      await settle(500);
      const first = await page.evaluate(() => window.dbWrites || []);
      ok(first.length >= 1, 'a part is written to the database', first.length);
      const row = first.length ? first[first.length - 1] : null;
      if (row) {
        const s = row.body.segments[0] || {};
        ok(row.path.startsWith('runs/live-'), 'one row per visit, runs/live-…', row.path);
        ok(row.body.bench === 'live' && row.body.kind === 'loop3d-bench', 'the row is a live recording');
        ok(s.scene === 'courtyard' && s.part === 0 && s.quality === 'Phone', 'the part names its scene, number and quality', [s.scene, s.part, s.quality]);
        ok(s.frames > 5 && s.gpuMs >= 0 && s.fps > 0, 'the part holds frames', [s.frames, s.fps, s.gpuMs]);
        ok(s.loadMs > 0, "the scene's first part has its load time", s.loadMs);
        ok(row.body.device && row.body.device.ua, 'the row says which device');
      }
      // the next scene: the last part of the one before, then the new one's
      await frame.evaluate(() => window.loopGoTo('cabin'));
      await until(frame, () => document.getElementById('note').style.display === 'none' && window.loopRecorder.visit.segments.some((s) => s.scene === 'courtyard' && s.part === 1), 'the scene change cuts a part');
      await settle(6000);
      await frame.evaluate(() => window.loopRecorder.cut());
      await settle(500);
      const all = await page.evaluate(() => window.dbWrites);
      const last = all[all.length - 1].body.segments;
      ok(last.map((s) => s.scene).join(',') === 'courtyard,courtyard,cabin', 'the parts follow the play', last.map((s) => [s.scene, s.part]));
      for (const x of last) console.log(`  saved: ${x.scene} part ${x.part}, ${x.quality}: ${x.frames} frames, ${x.fps} fps, GPU ${x.gpuMs} ms, load ${x.loadMs} ms`);
      const cabin = last[last.length - 1] || {};
      ok(cabin.part === 0 && cabin.loadMs > 0 && cabin.frames > 0, "the new scene's part starts at its first frame", [cabin.part, cabin.loadMs, cabin.frames]);
      const look = await frame.evaluate(() => ({
        isolated: window.crossOriginIsolated, worker: !!(navigator.serviceWorker && navigator.serviceWorker.controller),
        engine: [...document.scripts].map((s) => s.src.split('/').pop()).filter((n) => n.startsWith('player')),
        editor: getComputedStyle(document.getElementById('editorLink')).display,
        menu: [...document.getElementById('benchSel').options].map((o) => o.value),
        chip: document.getElementById('rec').textContent,
      }));
      ok(!look.isolated && !look.worker && look.engine.join() === 'player.js', 'in a frame: one core, no worker, no reload', look);
      ok(look.editor === 'none', 'in a frame: no editor link', look.editor);
      ok(look.menu.join() === ',copy', 'in a frame: only "Copy my recording" in the menu', look.menu);
      ok(look.chip.startsWith('● Recording for Claude'), 'the chip says it is saved', look.chip);
    }
    await page.close();
  }

  // ---- 2. on its own: the public player keeps the visit for "Copy my recording"
  {
    const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
    watch(page);
    await page.goto(base + 'index.html?quality=0#courtyard');
    if (await until(page, () => window.loopRecorder && window.Module && document.getElementById('note').style.display === 'none', 'the player starts')) {
      await settle(5000);
      await page.evaluate(() => window.loopRecorder.cut());
      const kept = await page.evaluate(() => { try { return JSON.parse(localStorage.getItem('loop3d.live')); } catch (e) { return null; } });
      ok(kept && kept.segments.length >= 1 && kept.segments[0].frames > 0, 'the visit is kept in the browser', kept && kept.segments.length);
      const look = await page.evaluate(() => ({ chip: document.getElementById('rec').hidden, menu: [...document.getElementById('benchSel').options].map((o) => o.value) }));
      ok(look.chip === true, 'no "saved for Claude" chip without a database');
      ok(look.menu.join() === ',copy,scene,story', 'the benchmarks stay in the public player', look.menu);
    }
    await page.close();
  }

  // ---- 3. the published page's models: the bar, read from text, byte for byte
  if (PAGE) {
    const page = await browser.newPage({ viewport: { width: 640, height: 360 } });
    watch(page);
    await page.goto(base + 'frame.html');
    const frame = await (await page.waitForSelector('#f')).contentFrame();
    await until(frame, () => window.loopRecorder && window.Module && document.getElementById('note').style.display === 'none', 'the framed page starts');
    await frame.evaluate(() => { const sc = document.getElementById('scene'); sc.value = 'bar'; sc.onchange(); });
    const barShown = () => { try { return window.Module.FS.readdir('/scene/models').some((n) => n.endsWith('.glb')) && document.getElementById('note').style.display === 'none'; } catch (e) { return false; } };
    if (await until(frame, barShown, 'the bar loads', 300000)) {
      await settle(4000);
      const sizes = await frame.evaluate(() => Object.fromEntries(window.Module.FS.readdir('/scene/models').filter((n) => n.endsWith('.glb')).map((n) => [n, window.Module.FS.stat('/scene/models/' + n).size])));
      const names = Object.keys(sizes);
      const wrong = names.filter((n) => sizes[n] !== fs.statSync(path.join(SITE, 'scenes/models', n)).size);
      ok(names.length > 100, "the bar's models arrive", names.length);
      ok(wrong.length === 0, 'each model whole, byte for byte', wrong.slice(0, 5));
      const group = await frame.evaluate(() => { const o = [...document.getElementById('scene').options].find((x) => x.value === 'bar'); return [o.parentNode.label, o.textContent, o.nextElementSibling && o.nextElementSibling.value]; });
      ok(group[1] === 'The whole bar' && group[2] === 'bar_room', 'the whole bar is first in its group, before the empty room', group);
      const problems = await frame.evaluate(() => document.getElementById('problems').textContent);
      ok(!/model/i.test(problems), 'no model missing', problems.slice(0, 200));
    }
    await page.close();
  }

  ok(errors.length === 0, 'no page errors', errors.slice(0, 5));
  console.log(`${checks} recorder checks in the player, ${failed} failed`);
  await browser.close();
  server.close();
  process.exit(failed ? 1 : 0);
})();
