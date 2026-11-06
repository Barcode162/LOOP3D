// Picture tests: each scene in tests/pictures/scenes.json, photographed by the
// live player and compared with its reference picture (tests/pictures/<name>.png).
// A film is photographed at its "filmtime" (seconds into its shots).
//
//   xvfb-run -a node tools/picture_check.js [--update] [scene ...]
//   PARAMS=clusters=1 ...   the same pictures with extra page switches (compare a new way of drawing)
//
// Needs the site built (tools/build_site.sh). The world is held still from its
// first moment (?pause=1), the resolution held at full (?res=1), the eyes
// allowed to settle (the exposure stops changing), then the frame is read
// back from the GPU and compared pixel by pixel. A scene passes when the
// pictures differ by less than `mean` (0..255, averaged over every channel)
// and fewer than `share` of the pixels differ by more than 24. --update makes
// the pictures taken the new references (say so in the log when a step
// changes the look on purpose). Differences are saved, brightened, in
// build/pictures/ beside the new pictures.
//
// Like tools/web_check.js it runs on a (virtual) display: headless Chromium
// loses the WebGPU device.
const { chromium } = require('playwright');
const fs = require('fs');
const http = require('http');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const SITE = process.env.SITE || path.join(ROOT, 'site');
const REFS = path.join(ROOT, 'tests/pictures');
const OUT = path.join(ROOT, 'build/pictures');
const args = process.argv.slice(2);
const update = args.includes('--update');
const config = JSON.parse(fs.readFileSync(path.join(REFS, 'scenes.json'), 'utf8'));
const only = args.filter((a) => !a.startsWith('--'));
const scenes = config.scenes.filter((s) => !only.length || only.includes(s.name));

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.png': 'image/png', '.jpg': 'image/jpeg', '.css': 'text/css', '.glb': 'model/gltf-binary' };
function serve() {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => {
      const file = path.join(SITE, decodeURIComponent(req.url.split('?')[0]).replace(/\/$/, '/index.html'));
      if (!file.startsWith(SITE) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
      res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
      fs.createReadStream(file).pipe(res);
    });
    server.listen(0, () => resolve(server));
  });
}

(async () => {
  if (!fs.existsSync(path.join(SITE, 'player.wasm'))) { console.log('build the site first: tools/build_site.sh'); process.exit(2); }
  if (!process.env.DISPLAY) console.log('warning: no display (run under xvfb-run -a): WebGPU loses its device headless');
  fs.mkdirSync(OUT, { recursive: true });
  const server = await serve();
  const port = server.address().port;
  const exe = fs.readdirSync('/opt/pw-browsers').filter((d) => d.startsWith('chromium-'))[0];
  const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    executablePath: `/opt/pw-browsers/${exe}/chrome-linux64/chrome`,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'],
  });
  const [w, h] = config.size;
  let failed = 0;
  for (const s of scenes) {
    const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
    const errors = [];
    page.on('pageerror', (e) => errors.push(String(e)));
    page.on('console', (m) => { if (m.type() === 'error' && !m.text().includes('Failed to load resource')) errors.push(m.text()); });
    await page.addInitScript(() => {
      let shown = null;
      Object.defineProperty(window, 'loopShown', { configurable: true, get() { return shown && (() => { window.loopFirstFrame = true; shown(); }); }, set(f) { shown = f; } });
    });
    const started = Date.now();
    await page.goto(`http://localhost:${port}/?quality=${config.quality}&res=1&msaa=${config.msaa}&pause=1&ui=0${process.env.PARAMS ? '&' + process.env.PARAMS : ''}#${s.name}`);
    const ok = await page.waitForFunction(() => window.loopFirstFrame === true, null, { timeout: 240000 }).then(() => true, () => false);
    if (!ok) { console.log(`FAIL ${s.name}: never showed a frame ${errors.slice(0, 2).join(' | ')}`); failed++; await page.close(); continue; }
    // the eyes settle fast, then wait until the exposure holds still
    await page.evaluate(() => {
      const M = window.Module, set = (id, v) => { const a = M.lengthBytesUTF8(id) + 1, p = M._malloc(a), b = M.lengthBytesUTF8(v) + 1, q = M._malloc(b); M.stringToUTF8(id, p, a); M.stringToUTF8(v, q, b); M._loop_set_setting(p, q); M._free(p); M._free(q); };
      set('cam.adaptSpeed', '10');
    });
    // a film is photographed at its own moment (past its fade from black)
    if (s.filmtime !== undefined) {
      await page.evaluate((t) => {
        const M = window.Module, n = M.lengthBytesUTF8('filmtime') + 1, p = M._malloc(n);
        M.stringToUTF8('filmtime', p, n);
        M._loop_set_option(p, t);
        M._free(p);
      }, s.filmtime);
    }
    let last = -1, steady = 0, frames0 = -1;
    for (let i = 0; i < 400 && steady < 3; i++) {
      await page.waitForTimeout(500);
      const st = await page.evaluate(() => JSON.parse(window.Module.UTF8ToString(window.Module._loop_stats())));
      if (frames0 < 0) frames0 = st.frames;
      const moved = Math.abs(st.exposure - last) / Math.max(1e-9, Math.abs(last));
      steady = st.frames - frames0 >= 8 && moved < 0.002 ? steady + 1 : 0;
      last = st.exposure;
    }
    // the frame itself, read back from the GPU (no page buttons on it)
    const png = await page.evaluate(() => new Promise((resolve) => {
      window.loopCapturedPixels = (cw, ch, px) => {
        const c = document.createElement('canvas');
        c.width = cw, c.height = ch;
        c.getContext('2d').putImageData(new ImageData(px, cw, ch), 0, 0);
        resolve(c.toDataURL('image/png'));
      };
      window.Module._loop_capture();
      setTimeout(() => resolve(null), 120000);
    }));
    const secs = ((Date.now() - started) / 1000).toFixed(0);
    if (!png) { console.log(`FAIL ${s.name}: no picture came back`); failed++; await page.close(); continue; }
    const taken = Buffer.from(png.split(',')[1], 'base64');
    fs.writeFileSync(path.join(OUT, s.name + '.png'), taken);
    const ref = path.join(REFS, s.name + '.png');
    if (update || !fs.existsSync(ref)) {
      fs.writeFileSync(ref, taken);
      console.log(`new  ${s.name}: reference saved (${secs} s)`);
      await page.close();
      continue;
    }
    // compare in the page (it can read both pictures)
    const refUrl = 'data:image/png;base64,' + fs.readFileSync(ref).toString('base64');
    const r = await page.evaluate(async ([a, b]) => {
      const load = (u) => new Promise((res) => { const im = new Image(); im.onload = () => res(im); im.src = u; });
      const [ia, ib] = await Promise.all([load(a), load(b)]);
      if (ia.width !== ib.width || ia.height !== ib.height) return { size: `${ia.width}x${ia.height} vs ${ib.width}x${ib.height}` };
      const c = document.createElement('canvas');
      c.width = ia.width, c.height = ia.height;
      const g = c.getContext('2d');
      g.drawImage(ia, 0, 0);
      const pa = g.getImageData(0, 0, c.width, c.height).data;
      g.drawImage(ib, 0, 0);
      const pb = g.getImageData(0, 0, c.width, c.height).data;
      const diff = g.createImageData(c.width, c.height);
      let sum = 0, big = 0;
      for (let i = 0; i < pa.length; i += 4) {
        const d = Math.max(Math.abs(pa[i] - pb[i]), Math.abs(pa[i + 1] - pb[i + 1]), Math.abs(pa[i + 2] - pb[i + 2]));
        sum += Math.abs(pa[i] - pb[i]) + Math.abs(pa[i + 1] - pb[i + 1]) + Math.abs(pa[i + 2] - pb[i + 2]);
        if (d > 24) big++;
        const v = Math.min(255, d * 8);
        diff.data[i] = diff.data[i + 1] = diff.data[i + 2] = v, diff.data[i + 3] = 255;
      }
      g.putImageData(diff, 0, 0);
      return { mean: sum / (pa.length / 4 * 3), share: big / (pa.length / 4), diff: c.toDataURL('image/png') };
    }, [png, refUrl]);
    if (r.size) { console.log(`FAIL ${s.name}: the size changed (${r.size})`); failed++; await page.close(); continue; }
    fs.writeFileSync(path.join(OUT, s.name + '_diff.png'), Buffer.from(r.diff.split(',')[1], 'base64'));
    const pass = r.mean <= config.mean && r.share <= config.share;
    if (!pass) failed++;
    console.log(`${pass ? 'ok  ' : 'FAIL'} ${s.name}: mean difference ${r.mean.toFixed(2)}, ${(r.share * 100).toFixed(2)}% of pixels changed (${secs} s)${errors.length ? ' · page: ' + errors[0].slice(0, 120) : ''}`);
    await page.close();
  }
  await browser.close();
  server.close();
  console.log(failed ? `${failed} of ${scenes.length} pictures changed` : `${scenes.length} pictures match`);
  process.exit(failed ? 1 : 0);
})();
