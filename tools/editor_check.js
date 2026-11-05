// Checks the editor in a real browser: loads it, applies settings, opens the
// settings window, runs a script with a mistake, opens a scene from another
// project, and lays it out on a phone. Screenshots go to <out dir>.
//   (cd site && python3 -m http.server 8123 &)
//   xvfb-run -a node tools/editor_check.js http://localhost:8123 <out dir>
// Run it on a virtual display: headless browsers lose WebGPU (docs/TUNING.md §8).
const { chromium } = require('playwright');
const BASE = process.argv[2] || 'http://localhost:8123', SP = process.argv[3] || 'out';
require("fs").mkdirSync(SP, { recursive: true });
(async () => {
  const browser = await chromium.launch({ headless: false, args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'] });
  const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });
  const errs = [];
  page.on('pageerror', (e) => errs.push('pageerror: ' + e.message));
  page.on('console', (m) => { if (m.type() === 'error') errs.push('console: ' + m.text()); });
  await page.goto(BASE + '/editor.html#courtyard');
  await page.waitForFunction(() => LE.engine.ready && document.getElementById('vpNote').style.display === 'none', null, { timeout: 120000 }).catch(() => errs.push('never showed a frame'));
  await page.waitForTimeout(4000);
  const r = {};
  r.stats = await page.evaluate(() => LE.engine.stats());
  r.title = await page.title();
  r.outliner = await page.evaluate(() => { LE.panels.outliner.refresh(); const i = LE.panels.outliner.info; return i && { things: i.things.length, lights: i.lights.length, people: i.people.length, shots: i.shots }; });
  await page.screenshot({ path: SP + '/ed_desktop.png' });
  // settings: options and look reach the engine
  r.setOpt = await page.evaluate(() => LE.engine.setOption('res', 0.5));
  r.setLook = await page.evaluate(() => { LE.store.set('look.preset', 'noir'); LE.store.set('look.bloom', 0.2); return LE.engine.setLook({ preset: 'noir', bloom: 0.2 }); });
  r.setWorld = await page.evaluate(() => { LE.store.set('fog.density', 0.03); return 1; });
  await page.evaluate(() => LE.store.set('render.quality', 0));
  await page.waitForTimeout(3000);
  r.statsAfter = await page.evaluate(() => LE.engine.stats());
  r.qualityToolbar = await page.evaluate(() => document.getElementById('tbQuality').value);
  await page.screenshot({ path: SP + '/ed_noir.png' });
  // settings window + search
  await page.keyboard.press('Control+,');
  await page.waitForTimeout(500);
  await page.screenshot({ path: SP + '/ed_settings.png' });
  await page.fill('.modal header input[type=search]', 'shadow');
  await page.waitForTimeout(300);
  r.searchHits = await page.evaluate(() => document.querySelectorAll('.modal .prop').length);
  await page.screenshot({ path: SP + '/ed_settings_search.png' });
  await page.keyboard.press('Escape');
  // reset look, edit and run a script with an error, then a fixed one
  await page.evaluate(() => { LE.store.resetMany(['look.preset', 'look.bloom', 'fog.density']); });
  await page.evaluate(() => { LE.dock.show('script'); const s = LE.panels.script; s.ta.value = s.ta.value + '\nbox size: vec(1,1,1), at: vec(0, 3, 0), materal: "gold"\nlet = 3\n'; s.ta.dispatchEvent(new Event('input')); return s.run(); });
  await page.waitForTimeout(3000);
  r.problems = await page.evaluate(() => LE.panels.script.problems);
  await page.screenshot({ path: SP + '/ed_script.png' });
  // a scene from the other project
  await page.evaluate(() => { localStorage.clear(); LE.panels.script.dirty = false; return LE.run('openScene', 'ardane_02_market'); });
  await page.waitForFunction(() => document.getElementById('vpNote').style.display === 'none', null, { timeout: 120000 }).catch(() => errs.push('ardane never showed'));
  await page.waitForTimeout(3000);
  r.ardane = await page.evaluate(() => ({ scene: LE.engine.scene, project: LE.store.project, chip: document.getElementById('projectChip').textContent, stats: LE.engine.stats() }));
  await page.evaluate(() => { LE.dock.show('timeline'); });
  await page.waitForTimeout(800);
  await page.screenshot({ path: SP + '/ed_ardane.png' });
  r.ardaneErr = await page.evaluate(async () => { const s = LE.panels.script; const n = s.ta.value.split('\n').length; s.ta.value += '\nlet = 3\n'; await s.run(); await new Promise((r) => setTimeout(r, 2500)); return { expectLine: n + 1, problems: s.problems, marked: [...s.errLines] }; });
  await page.evaluate(() => { LE.dock.show('look'); LE.store.set('look.preset', 'neon'); });
  await page.waitForTimeout(2500);
  await page.screenshot({ path: SP + '/ed_look.png' });
  await page.evaluate(() => LE.dock.show('profiler'));
  await page.waitForTimeout(1500);
  await page.screenshot({ path: SP + '/ed_profiler.png' });
  r.console = await page.evaluate(() => LE.panels.console.entries.slice(-12).map((e) => e.kind + ': ' + e.text));
  // phone
  const ph = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2, hasTouch: true, isMobile: true });
  ph.on('pageerror', (e) => errs.push('phone pageerror: ' + e.message));
  await ph.goto(BASE + '/editor.html#cabin');
  await ph.waitForFunction(() => LE.engine.ready && document.getElementById('vpNote').style.display === 'none', null, { timeout: 120000 }).catch(() => errs.push('phone never showed'));
  await ph.waitForTimeout(3000);
  r.phoneOverflow = await ph.evaluate(() => document.documentElement.scrollWidth > innerWidth);
  await ph.screenshot({ path: SP + '/ed_phone.png' });
  await ph.evaluate(() => LE.run('projectSettings'));
  await ph.waitForTimeout(500);
  await ph.screenshot({ path: SP + '/ed_phone_settings.png' });
  r.errs = errs;
  console.log(JSON.stringify(r, null, 1));
  await browser.close();
})();
