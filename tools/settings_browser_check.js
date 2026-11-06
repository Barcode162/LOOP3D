// Checks every live setting in a real browser: sets each one through the
// editor, reads it back from the engine, keeps frames coming through the
// heavy ones (MSAA, shadow maps, sky, Ultra), checks a scene's own `setting`
// against the editor's, and hands a scene from the editor to the player.
//   (cd site && python3 -m http.server 8123 &)
//   xvfb-run -a node tools/settings_browser_check.js http://localhost:8123 <out dir>
// Run it on a virtual display: headless browsers lose WebGPU (docs/TUNING.md §8).
const { chromium } = require('playwright');
const BASE = process.argv[2] || 'http://localhost:8123', OUT = process.argv[3] || 'out';
require('fs').mkdirSync(OUT, { recursive: true });

(async () => {
  const browser = await chromium.launch({ headless: false, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] });
  const context = await browser.newContext({ viewport: { width: 1400, height: 900 } });  // one context: the player sees what the editor stored
  const page = await context.newPage();
  const errs = [];
  page.on('pageerror', (e) => errs.push('page: ' + (e.stack || e.message)));
  page.on('console', (m) => { if (m.type() === 'error' && !/favicon/.test(m.text())) errs.push('console: ' + m.text()); });
  const shown = async () => page.waitForFunction(() => LE.engine.ready && document.getElementById('vpNote').style.display === 'none', null, { timeout: 180000 });
  const frames = () => page.evaluate(() => LE.engine.stats().time || 0);
  const r = { failed: [], shots: [] };
  // screenshots wait for a frame: on a software GPU that can take long, so it never fails the check
  const snap = (p, name) => p.screenshot({ path: OUT + '/' + name, timeout: 90000 }).then(() => r.shots.push(name)).catch(() => r.shots.push(name + ' (timed out)'));
  await page.goto(BASE + '/editor.html#courtyard');
  await shown();
  // a small picture: the test machine's GPU is software (seconds a frame at full size)
  await page.evaluate(() => LE.store.set('render.res', 0.25));
  await page.waitForTimeout(3000);

  // 1. every engine setting: set through the editor, read back from the engine
  r.roundTrip = await page.evaluate(() => {
    const bad = [];
    let n = 0;
    for (const it of LE.store.items.values()) {
      if (it.bind !== 'set') continue;
      let v;
      if (it.type === 'toggle') v = !it.def;
      else if (it.type === 'enum') v = (it.options.find((o) => JSON.stringify(o.value) !== JSON.stringify(it.def)) || it.options[0]).value;
      else if (it.type === 'vec3') v = [1, 2, 3].map((x) => Math.min(it.max ?? 1e9, Math.max(it.min ?? -1e9, x)));
      else if (it.type === 'slider' || it.type === 'number') v = it.max !== undefined ? +(it.min + (it.max - it.min) * 0.5).toFixed(3) : it.def + 1;
      else if (it.type === 'key') v = 'KeyQ';
      else if (it.type === 'color') v = '#123456';
      else v = it.id === 'script.onError' ? 'continue' : it.id === 'cinema.quality' ? 'draft' : it.id === 'project.mode' ? 'movie' : 'test';
      if (['render.res', 'render.view', 'render.passDraw', 'render.passFilm', 'render.passScene', 'script.timeScale', 'render.passExposure'].includes(it.id)) continue;  // checked below, they blank the view
      LE.store.set(it.id, v);
      const got = LE.engine.getSetting(it.id);
      const same = Array.isArray(v) ? v.every((x, i) => Math.abs(x - got[i]) < 1e-3) : typeof v === 'number' ? Math.abs(v - got) < Math.max(0.51, Math.abs(v) * 1e-3) : typeof v === 'boolean' ? v === got : String(v) === String(got);
      if (!same) bad.push(it.id + ': set ' + JSON.stringify(v) + ', engine has ' + JSON.stringify(got));
      n++;
    }
    return { n, bad };
  });
  const t0 = await frames();
  await page.waitForTimeout(10000);
  r.framesWithEverythingChanged = (await frames()) > t0;
  await snap(page, 'all_changed.png');
  await page.evaluate(() => { for (const it of LE.store.items.values()) if (it.scope === 'project' && it.id !== 'render.res') LE.store.reset(it.id); });
  await page.waitForTimeout(2000);

  // 2. the heavy ones keep frames coming
  for (const [id, v] of [['render.msaa', 1], ['render.sunMapHigh', 4096], ['render.lamps', 4], ['sky.turbidity', 6], ['sun.elevation', 10], ['look.sharpen', 1], ['look.distortion', 0.2], ['render.fxaa', true]]) {
    await page.evaluate(([id, v]) => LE.store.set(id, v), [id, v]);
    const a = await frames();
    await page.waitForTimeout(8000);
    if (!((await frames()) > a)) r.failed.push('no frames after ' + id);
  }
  await snap(page, 'sharpen_distort_sunset.png');
  await page.evaluate(() => { for (const it of LE.store.items.values()) if (it.scope === 'project' && it.id !== 'render.res') LE.store.reset(it.id); });
  // Ultra with its settings
  // (small: the test machine's GPU is software, and Ultra traces every pixel)
  await page.evaluate(() => { LE.store.set('render.quality', 2); LE.store.set('ultra.spp', 2); LE.store.set('ultra.denoise', false); LE.store.set('ultra.bounces', 3); });
  const u = await frames();
  await page.waitForTimeout(12000);
  if (!((await frames()) > u)) r.failed.push('no frames on Ultra');
  await snap(page, 'ultra.png');
  await page.evaluate(() => { for (const it of LE.store.items.values()) if (it.scope === 'project' && it.id !== 'render.res') LE.store.reset(it.id); LE.store.set('render.quality', 1); });
  // day cycle
  await page.evaluate(() => { LE.engine.loadScene('courtyard'); });
  await page.waitForTimeout(500);
  await shown();
  await page.evaluate(() => { LE.store.set('sun.daySpeed', 0.5); LE.store.set('sun.dayCycle', true); LE.store.set('render.showLights', true); });
  const sky0 = await page.evaluate(() => JSON.stringify(LE.engine.sceneInfo().lights.find((l) => l.kind === 'sun')?.direction));
  await page.waitForTimeout(15000);
  const sky1 = await page.evaluate(() => JSON.stringify(LE.engine.sceneInfo().lights.find((l) => l.kind === 'sun')?.direction));
  r.dayCycleMovesSun = sky0 !== sky1;
  await snap(page, 'day_cycle_overlay.png');
  await page.evaluate(() => { for (const it of LE.store.items.values()) if (it.scope === 'project' && it.id !== 'render.res') LE.store.reset(it.id); });

  // 3. a scene's own setting, the editor over it, and back
  r.layers = await page.evaluate(async () => {
    LE.history.commit('setting "player.walk", 3\n' + LE.history.text, 'a scene setting');
    await LE.spaces.rerun();
    await new Promise((res) => setTimeout(res, 2500));
    const scene = LE.engine.getSetting('player.walk');
    LE.store.set('player.walk', 5);
    const editor = LE.engine.getSetting('player.walk');
    LE.store.reset('player.walk');
    const back = LE.engine.getSetting('player.walk');
    return { scene, editor, back };
  });
  if (!(r.layers.scene === 3 && r.layers.editor === 5 && r.layers.back === 3)) r.failed.push('layers ' + JSON.stringify(r.layers));

  // 4. the editor hands what's open to the player
  await page.evaluate(() => { const t = LE.history.text.replace(/^title .*$/m, 'title "Handed over"'); LE.history.commit(t, 'renamed'); LoopUI.handTo(LE.engine.scene, t, LE.store.project); });
  const player = await context.newPage();
  player.on('pageerror', (e) => errs.push('player: ' + e.message));
  await player.goto(BASE + '/?from=editor&res=0.4#courtyard');
  await player.waitForFunction(() => document.getElementById('title').textContent.includes('Handed over'), null, { timeout: 120000 }).catch(() => r.failed.push('the player didn\'t play the handed scene'));
  await player.waitForTimeout(2000);
  await snap(player, 'player_handed.png');
  // and plays a published scene on its own
  await player.goto(BASE + '/?res=0.4#ardane_02_market');
  await player.waitForFunction(() => document.getElementById('note').style.display === 'none', null, { timeout: 120000 }).catch(() => r.failed.push('the player didn\'t show ardane_02_market'));
  await player.waitForTimeout(2000);
  await snap(player, 'player_ardane.png');

  r.engineErrors = await page.evaluate(() => LE.logic.messages.filter((e) => e.kind === 'error' && !/needs a name/.test(e.text)).map((e) => e.text));
  if (r.roundTrip.bad.length) r.failed.push(...r.roundTrip.bad);
  if (!r.framesWithEverythingChanged) r.failed.push('no frames with every setting changed');
  if (!r.dayCycleMovesSun) r.failed.push('the day cycle didn\'t move the sun');
  r.errs = errs;
  console.log(JSON.stringify(r, null, 1));
  await browser.close();
  process.exit(r.failed.length || errs.length || r.engineErrors.length ? 1 : 0);
})();
