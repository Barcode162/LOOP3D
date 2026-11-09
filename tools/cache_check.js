// The light cache (phase 6) relit only near a change: the cabin plays until
// its probes are baked, then its candle goes out (its own script, changed);
// the probes within the candle's reach must be baked again, and only they
// (the fire's reach is the whole cabin: putting it out relights all of it).
//   serve site/ on a port, then: xvfb-run -a node tools/cache_check.js http://localhost:8125
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
(async () => {
  const base = process.argv[2] || 'http://localhost:8125';
  const exe = fs.readdirSync('/opt/pw-browsers').filter((d) => d.startsWith('chromium-'))[0];
  const b = await chromium.launch({ headless: false, executablePath: `/opt/pw-browsers/${exe}/chrome-linux64/chrome`,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] });
  const p = await b.newPage({ viewport: { width: 320, height: 240 }, deviceScaleFactor: 1 });
  const errors = [];
  p.on('pageerror', (e) => errors.push(String(e)));
  await p.addInitScript(() => { let shown = null; Object.defineProperty(window, 'loopShown', { configurable: true, get() { return shown && (() => { window.loopFirstFrame = true; shown(); }); }, set(f) { shown = f; } }); });
  await p.goto(`${base}/?quality=0&res=1&ui=0&cachems=60000#cabin`);
  await p.waitForFunction(() => window.loopFirstFrame === true, null, { timeout: 280000 });
  const st = () => p.evaluate(() => JSON.parse(window.Module.UTF8ToString(window.Module._loop_stats())));
  const settled = async () => { for (let i = 0; i < 240; i++) { const s = await st(); if (s.clustered && s.cacheProbes && !s.cacheWaiting) return s; await p.waitForTimeout(500); } return st(); };
  // the cabin's script, its candle put out after 4 seconds (the flicker kept before then)
  const src = fs.readFileSync(path.join(__dirname, '..', 'site', 'scenes', 'cabin.loop'), 'utf8')
    .replace('  candle.lumens = 14 * (1 + 0.15 * flicker(time(), 9, 2))', '  candle.lumens = 14 * (1 + 0.15 * flicker(time(), 9, 2)) if time() < 4 else 0');
  await p.evaluate((s) => { const M = window.Module, n = M.lengthBytesUTF8(s) + 1, q = M._malloc(n); M.stringToUTF8(s, q, n); M._loop_run_script(q); M._free(q); }, src);
  await p.waitForTimeout(1500);
  const before = await settled();
  // the fire goes out at 4 s: wait for the relight to start and finish
  let after = before;
  for (let i = 0; i < 240 && after.cacheRelit === before.cacheRelit; i++) { await p.waitForTimeout(500); after = await st(); }
  after = await settled();
  const relit = after.cacheRelit - before.cacheRelit;
  const ok = relit > 0 && relit < after.cacheProbes / 2 && !errors.length;
  console.log(`${ok ? 'ok  ' : 'FAIL'} the candle went out: ${relit} of ${after.cacheProbes} probes baked again (time ${after.time.toFixed(1)} s)${errors.length ? ' errors: ' + errors.slice(0, 2).join(' | ') : ''}`);
  process.exitCode = ok ? 0 : 1;
  await b.close();
})();
