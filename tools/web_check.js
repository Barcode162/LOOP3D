// Opens the player in Chromium (WebGPU on SwiftShader), lets it run, and saves
// what the canvas shows (copied inside the page). Prints the page's problems.
//   xvfb-run -a node tools/web_check.js <url> <out.png> [seconds] [width] [height]
//
// Run it on a (virtual) display, as above. Headless Chromium loses the WebGPU
// device the moment a canvas is configured ("A valid external Instance
// reference no longer exists"): the GPU never says a frame is done, so every
// number measured that way (fps, GPU ms, hitches) is the watchdog's, not the
// engine's. SwiftShader also runs the whole pixel shader once per MSAA sample,
// so add &msaa=1 to judge shader costs the way a real GPU pays them.
//
// Before/after pictures of a film (a scene that plays itself) must be taken at
// the same moment: AT="filmtime:20,pause:1" sets the player's own controls
// (loop_set_option, as the editor's play bar does) once it has started, so
// the camera holds still and the eyes settle before the picture.
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [url, out, wait = '6', w = '640', h = '360'] = process.argv.slice(2);
  const exe = fs.readdirSync('/opt/pw-browsers').filter((d) => d.startsWith('chromium-'))[0];
  if (!process.env.DISPLAY) console.log('warning: no display, so headless: WebGPU canvases lose their device there (run under xvfb-run -a)');
  const browser = await chromium.launch({
    headless: !process.env.DISPLAY,
    executablePath: `/opt/pw-browsers/${exe}/chrome-linux64/chrome`,
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface'],
  });
  const page = await browser.newPage({ viewport: { width: +w, height: +h } });
  const errors = [];
  page.on('console', (m) => { if (process.env.VERBOSE) console.log(m.type(), m.text().slice(0, 300)); if (m.type() === 'error' && !m.text().includes('Failed to load resource')) errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('crash', () => errors.push('the page crashed'));
  await page.goto(url);
  if (process.env.AT) {
    await page.waitForFunction(() => window.Module && window.Module._loop_set_option, null, { timeout: 120000 }).catch(() => errors.push('AT: the player never started'));
    await page.waitForTimeout(3000);
    for (const pair of process.env.AT.split(',')) {
      const [name, value] = pair.split(':');
      await page.evaluate(([n, v]) => {
        const M = window.Module, len = M.lengthBytesUTF8(n) + 1, ptr = M._malloc(len);
        M.stringToUTF8(n, ptr, len);
        M._loop_set_option(ptr, +v);
        M._free(ptr);
      }, [name, value]);
    }
  }
  await page.waitForTimeout(parseFloat(wait) * 1000);
  let data = null;
  if (process.env.DISPLAY) {
    // a real (virtual) screen: ordinary screenshots include WebGPU canvases
    await page.screenshot({ path: out, timeout: 120000 });
  } else {
    // headless: the engine reads its own frame back from the GPU
    data = await page.evaluate(() => new Promise((resolve) => {
      window.loopCapturedPixels = (w, h, px) => {
        const c = document.createElement('canvas');
        c.width = w, c.height = h;
        c.getContext('2d').putImageData(new ImageData(px, w, h), 0, 0);
        resolve(c.toDataURL('image/png'));
      };
      Module._loop_capture();
      setTimeout(() => resolve(null), 120000);
    })).catch((e) => { errors.push('capture: ' + e); return null; });
  }
  if (data) fs.writeFileSync(out, Buffer.from(data.split(',')[1], 'base64'));
  const stats = await page.textContent('#stats').catch(() => '');
  const problems = await page.textContent('#problems').catch(() => '');
  const lines = await page.textContent('#lines').catch(() => '');
  const ask = await page.textContent('#ask').catch(() => '');
  if (lines) console.log('on screen:', lines);
  if (ask) console.log('asking:', ask);
  console.log('stats:', stats);
  if (problems) console.log('script problems:', problems);
  for (const e of errors.slice(0, 20)) console.log('page:', e.slice(0, 400));
  await browser.close();
})();
