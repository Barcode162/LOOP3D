// The sun's kept shadow (phase 5): with things moving, the shadow of what
// stands still is kept and only the movers are drawn on a copy of it each
// frame. This plays a scene, pauses, and photographs the same moment that way
// and drawn all again (skip bit 2048): they must match. It also counts how
// often the whole shadow was drawn while playing.
//   serve site/ on a port, then:
//   xvfb-run -a node tools/shadow_check.js http://localhost:8125 [scene]
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const exe = fs.readdirSync('/opt/pw-browsers').filter((d) => d.startsWith('chromium-'))[0];
  const b = await chromium.launch({ headless: false, executablePath: `/opt/pw-browsers/${exe}/chrome-linux64/chrome`,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] });
  const base = process.argv[2] || 'http://localhost:8125';
  const scene = process.argv[3] || 'courtyard';
  const p = await b.newPage({ viewport: { width: 480, height: 320 }, deviceScaleFactor: 1 });
  await p.addInitScript(() => { let shown = null; Object.defineProperty(window, 'loopShown', { configurable: true, get() { return shown && (() => { window.loopFirstFrame = true; shown(); }); }, set(f) { shown = f; } }); });
  await p.goto(`${base}/?quality=0&res=1&ui=0#${scene}`);
  await p.waitForFunction(() => window.loopFirstFrame === true, null, { timeout: 280000 });
  await p.waitForFunction(() => JSON.parse(window.Module.UTF8ToString(window.Module._loop_stats())).clustered === 1, null, { timeout: 280000 });
  const opt = (k, v) => p.evaluate(([k, v]) => { const M = window.Module, n = M.lengthBytesUTF8(k) + 1, q = M._malloc(n); M.stringToUTF8(k, q, n); M._loop_set_option(q, v); M._free(q); }, [k, v]);
  const st = () => p.evaluate(() => JSON.parse(window.Module.UTF8ToString(window.Module._loop_stats())));
  await p.waitForTimeout(6000);
  const a = await st();
  await p.waitForTimeout(4000);
  const a2 = await st();
  console.log('live: frames', a2.frames - a.frames, 'full shadow draws', a2.shadowDraws - a.shadowDraws);
  await opt('pause', 1);
  await p.waitForTimeout(3000);  // frame blending settles on the paused world
  const cap = () => p.evaluate(() => new Promise((resolve) => {
    window.loopCapturedPixels = (cw, ch, px) => { const c = document.createElement('canvas'); c.width = cw, c.height = ch; c.getContext('2d').putImageData(new ImageData(px, cw, ch), 0, 0); resolve(c.toDataURL('image/png')); };
    window.Module._loop_capture();
  }));
  const s1 = await cap();
  const d1 = await st();
  await opt('skip', 2048);
  await p.waitForTimeout(3000);
  const s2 = await cap();
  const d2 = await st();
  console.log('paused, split then full: full shadow draws during full', d2.shadowDraws - d1.shadowDraws);
  const r = await p.evaluate(async ([a, b]) => {
    const load = (u) => new Promise((res) => { const im = new Image(); im.onload = () => res(im); im.src = u; });
    const [ia, ib] = await Promise.all([load(a), load(b)]);
    const c = document.createElement('canvas'); c.width = ia.width, c.height = ia.height; const g = c.getContext('2d');
    g.drawImage(ia, 0, 0); const pa = g.getImageData(0, 0, c.width, c.height).data; g.drawImage(ib, 0, 0); const pb = g.getImageData(0, 0, c.width, c.height).data;
    let sum = 0, big = 0;
    for (let i = 0; i < pa.length; i += 4) { const d = Math.max(Math.abs(pa[i] - pb[i]), Math.abs(pa[i + 1] - pb[i + 1]), Math.abs(pa[i + 2] - pb[i + 2])); sum += d; if (d > 24) big++; }
    return { mean: sum / (pa.length / 4), share: 100 * big / (pa.length / 4) };
  }, [s1, s2]);
  const ok = r.share <= 0.1 && r.mean <= 1.5;
  console.log(`${ok ? 'ok  ' : 'FAIL'} kept shadow vs drawn again: mean ${r.mean.toFixed(2)}, ${r.share.toFixed(2)}% of pixels changed`);
  process.exitCode = ok ? 0 : 1;
  await b.close();
})();
