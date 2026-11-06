// Many cores for the engine. Browsers let a page's cores share memory (what
// the many-core player needs) only when the page is "cross-origin isolated":
// its server sends two headers saying it loads nothing from other sites
// without their consent. GitHub Pages can't send headers, so this small
// service worker, served from our own site, adds them to every answer from
// it. On a first visit the worker installs and the page reloads once; after
// that the page opens isolated straight away. A browser that won't (or a
// second failed try) gets the one-core player: nothing else changes.
//
// The page side: `await LoopIsolate.ready()` → true when the cores can share.
if (typeof window === 'undefined') {
  // ---- the service worker
  self.addEventListener('install', () => self.skipWaiting());
  self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));
  self.addEventListener('fetch', (e) => {
    const r = e.request;
    if (r.cache === 'only-if-cached' && r.mode !== 'same-origin') return;
    if (new URL(r.url).origin !== self.location.origin) return;  // only our own site's answers
    e.respondWith(fetch(r).then((res) => {
      if (res.status === 0) return res;
      const h = new Headers(res.headers);
      h.set('Cross-Origin-Opener-Policy', 'same-origin');
      h.set('Cross-Origin-Embedder-Policy', 'require-corp');
      h.set('Cross-Origin-Resource-Policy', 'same-origin');
      return new Response(res.body, { status: res.status, statusText: res.statusText, headers: h });
    }));
  });
} else {
  // ---- the page
  window.LoopIsolate = {
    async ready() {
      if (window.crossOriginIsolated) return true;
      const params = new URLSearchParams(location.search);
      if (params.get('cores') === '1' || !window.isSecureContext || !('serviceWorker' in navigator)) return false;
      let tried = false;
      try { tried = sessionStorage.getItem('loopIsolateTried') === '1'; } catch (e) { tried = true; }
      if (tried) return false;  // one reload at most: never a loop
      try {
        const reg = await navigator.serviceWorker.register(new URL('isolate.js', location.href).pathname);
        if (navigator.serviceWorker.controller && !reg.installing && !reg.waiting) {
          // already in charge, yet not isolated: this browser won't
          sessionStorage.setItem('loopIsolateTried', '1');
          return false;
        }
        await navigator.serviceWorker.ready;
        sessionStorage.setItem('loopIsolateTried', '1');
        location.reload();
        return new Promise(() => {});  // the page is going away
      } catch (e) {
        return false;
      }
    },
  };
}
