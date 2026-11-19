// The light lab (docs/LIGHT_LAB.md): one way of lighting, measured. Photographs a scene in the live player
// with a set of page switches (settled the way the picture tests settle: exposure still, light cache baked),
// then scores it against the film picture with `loop score` (ΔE: 0 the same, ~2 barely seen, 10+ plainly off).
//   xvfb-run -a node tools/light_lab.js <scene> <name> ["switch=v&switch=v"]
// Writes perf/lab/<scene>/<name>.png and prints one JSON line. Needs site/ (tools/build_site.sh).
const { chromium } = require('playwright');
const fs = require('fs'), http = require('http'), path = require('path'), { execFileSync } = require('child_process');
const ROOT = path.resolve(__dirname, '..'), SITE = path.join(ROOT, 'site');
const [scene, name, params = ''] = process.argv.slice(2);
const [W, H] = [288, 512];
// SRC=<file.loop>: a variant scene (perf/lab/scenes/), copied into the built site under <scene>.loop
if (process.env.SRC) fs.copyFileSync(path.resolve(process.env.SRC), path.join(SITE, 'scenes', scene + '.loop'));
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.png': 'image/png', '.css': 'text/css', '.glb': 'model/gltf-binary' };
(async () => {
  const server = http.createServer((req, res) => {
    const f = path.join(SITE, decodeURIComponent(req.url.split('?')[0]).replace(/\/$/, '/index.html'));
    if (!f.startsWith(SITE) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream' });
    fs.createReadStream(f).pipe(res);
  }).listen(0);
  await new Promise((r) => server.on('listening', r));
  const exe = fs.readdirSync('/opt/pw-browsers').filter((d) => d.startsWith('chromium-'))[0];
  const browser = await chromium.launch({ headless: !process.env.DISPLAY, executablePath: `/opt/pw-browsers/${exe}/chrome-linux64/chrome`,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] });
  const page = await browser.newPage({ viewport: { width: W, height: H }, deviceScaleFactor: 1 });
  page.on('console', (m) => { if (m.type() === 'error' || /WGSL|shader/i.test(m.text())) process.stderr.write('page: ' + m.text().slice(0, 400) + '\n'); });
  await page.addInitScript(() => {
    let shown = null;
    Object.defineProperty(window, 'loopShown', { configurable: true, get() { return shown && (() => { window.loopFirstFrame = true; shown(); }); }, set(f) { shown = f; } });
  });
  await page.goto(`http://localhost:${server.address().port}/?quality=${process.env.QUALITY || 0}&res=1&msaa=1&pause=1&ui=0&cachems=60000${params ? '&' + params : ''}#${scene}`);
  await page.waitForFunction(() => window.loopFirstFrame === true, null, { timeout: 240000 });
  await page.evaluate(() => {
    const M = window.Module, set = (id, v) => { const a = M.lengthBytesUTF8(id) + 1, p = M._malloc(a), b = M.lengthBytesUTF8(v) + 1, q = M._malloc(b); M.stringToUTF8(id, p, a); M.stringToUTF8(v, q, b); M._loop_set_setting(p, q); M._free(p); M._free(q); };
    set('cam.adaptSpeed', '10');
  }).catch(() => {});
  let last = -1, steady = 0, frames0 = -1, st = {};
  for (let i = 0; i < (+process.env.FRAMES ? 4000 : 2400) && steady < 3; i++) {
    await page.waitForTimeout(500);
    st = await page.evaluate(() => JSON.parse(window.Module.UTF8ToString(window.Module._loop_stats())));
    if (frames0 < 0) frames0 = st.frames;
    const moved = Math.abs(st.exposure - last) / Math.max(1e-9, Math.abs(last));
    if (st.cacheWaiting || st.farWaiting || st.bakeWaiting) frames0 = st.frames;
    steady = st.frames - frames0 >= (+process.env.FRAMES || 32) && moved < 0.002 && (st.clustered || params.includes('clusters=0')) ? steady + 1 : 0;
    last = st.exposure;
  }
  const png = await page.evaluate(() => new Promise((resolve) => {
    window.loopCapturedPixels = (cw, ch, px) => { const c = document.createElement('canvas'); c.width = cw, c.height = ch; c.getContext('2d').putImageData(new ImageData(px, cw, ch), 0, 0); resolve(c.toDataURL('image/png')); };
    window.Module._loop_capture();
    setTimeout(() => resolve(null), 120000);
  }));
  await browser.close(); server.close();
  const dir = path.join(ROOT, 'perf/lab', scene); fs.mkdirSync(dir, { recursive: true });
  const file = path.join(dir, name + '.png');
  fs.writeFileSync(file, Buffer.from(png.split(',')[1], 'base64'));
  const src = process.env.SRC ? path.resolve(process.env.SRC) : [`projects/loft/places/${scene}.loop`, `projects/samples/${scene}.loop`].map((p) => path.join(ROOT, p)).find(fs.existsSync)
    || execFileSync('bash', ['-c', `ls ${ROOT}/projects/*/places/${scene}.loop | head -1`]).toString().trim();
  const out = execFileSync(path.join(ROOT, 'build/loop'), ['score', src, file, '--samples', process.env.SAMPLES || '256', '-o', path.join(dir, 'film'), '--reuse']).toString();
  const m = out.match(/: ([\d.]+) \(worst tenth over ([\d.]+); brightness live\/cinema ([\d.]+)/);
  console.log(JSON.stringify({ scene, name, params, score: m && +m[1], worst: m && +m[2], brightness: m && +m[3], lights: st.lights, cacheProbes: st.cacheProbes, bakeCells: st.bakeCells, exposure: st.exposure }));
})();
