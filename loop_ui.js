// What the player page and the editor share: dialogue lines and choices on
// screen, the fade between scenes, keys, the gamepad and the thumb sticks,
// all following the engine settings (story.*, keys.*, player.*), so a
// project, a scene's script or the editor can change them.
window.LoopUI = (() => {
  // a setting's value in effect (engine/core/settings.h), or the fallback
  function setting(M, id, fallback) {
    if (!M || typeof M._loop_get_setting !== 'function') return fallback;
    const n = M.lengthBytesUTF8(id) + 1, p = M._malloc(n);
    M.stringToUTF8(id, p, n);
    try {
      const v = JSON.parse(M.UTF8ToString(M._loop_get_setting(p)));
      return v === null || v === undefined ? fallback : v;
    } catch (e) {
      return fallback;
    } finally {
      M._free(p);
    }
  }

  // a line of dialogue: fades in, stays long enough to read, fades out
  function showLine(box, M, m) {
    const d = document.createElement('div');
    d.className = 'line';
    d.style.fontSize = (14 * setting(M, 'story.subtitleSize', 1)) + 'px';
    if (m.speaker && setting(M, 'story.speakerNames', true)) {
      const w = document.createElement('span');
      w.className = 'who';
      w.style.color = setting(M, 'story.speakerColor', '#ffd27a');
      w.textContent = m.speaker;
      d.appendChild(w);
    }
    d.appendChild(document.createTextNode(m.text));
    box.appendChild(d);
    while (box.children.length > 2) box.firstChild.remove();
    setTimeout(() => { d.style.opacity = '0'; setTimeout(() => d.remove(), 700); }, m.seconds * 1000);
  }

  // a question with buttons; story.choiceTimeout picks for you when time runs out
  let askTimer = 0;
  function showAsk(box, M, m, pick) {
    clearTimeout(askTimer);
    box.innerHTML = '';
    if (m.text) { const q = document.createElement('div'); q.className = 'q'; q.textContent = m.text; box.appendChild(q); }
    const choose = (i) => { clearTimeout(askTimer); box.style.display = 'none'; pick(i); };
    m.options.forEach((o, i) => {
      const b = document.createElement('button');
      b.textContent = o;
      b.onclick = () => choose(i);
      box.appendChild(b);
    });
    box.style.display = 'flex';
    const secs = setting(M, 'story.choiceTimeout', 0);
    if (secs > 0) {
      const which = setting(M, 'story.timeoutPick', 0);
      const bar = document.createElement('div');
      bar.className = 'timeout';
      bar.style.cssText = 'height:3px;background:currentColor;opacity:.5;transition:width ' + secs + 's linear;width:100%';
      box.appendChild(bar);
      requestAnimationFrame(() => { bar.style.width = '0%'; });
      askTimer = setTimeout(() => choose(which < 0 ? Math.floor(Math.random() * m.options.length) : Math.min(which, m.options.length - 1)), secs * 1000);
    }
  }

  // the text shown while a scene loads (story.loadingText, {scene} = its title)
  const loadingText = (M, title) => String(setting(M, 'story.loadingText', 'Loading {scene}…')).replace('{scene}', title || 'the next scene');

  // a black veil for the fade between scenes (story.fade seconds)
  function fader(parent) {
    let v = parent.querySelector('.loop-fade');
    if (!v) {
      v = document.createElement('div');
      v.className = 'loop-fade';
      v.style.cssText = 'position:absolute;inset:0;background:#000;opacity:0;pointer-events:none;transition:opacity .5s';
      parent.appendChild(v);
    }
    return {
      out(M) { const s = setting(M, 'story.fade', 0.6); v.style.transitionDuration = s + 's'; v.style.opacity = s > 0 ? '1' : '0'; return new Promise((r) => setTimeout(r, s * 1000)); },
      in(M) { v.style.transitionDuration = setting(M, 'story.fade', 0.6) + 's'; v.style.opacity = '0'; },
    };
  }

  // keys.* → the engine's walking keys (0 forward, 1 back, 2 left, 3 right, 4 run)
  function keyMap(M) {
    const m = { ArrowUp: 0, ArrowDown: 1, ArrowLeft: 2, ArrowRight: 3, ShiftRight: 4 };
    ['keys.forward', 'keys.back', 'keys.left', 'keys.right', 'keys.run'].forEach((id, i) => { m[setting(M, id, ['KeyW', 'KeyS', 'KeyA', 'KeyD', 'ShiftLeft'][i])] = i; });
    return m;
  }
  const useKey = (M) => setting(M, 'keys.use', 'KeyE');

  // a gamepad: left stick walks, right stick looks, A uses, a shoulder runs
  function gamepad(getM, onUse, onActive) {
    let wasA = false, moving = false;
    function poll() {
      const M = getM();
      const pads = navigator.getGamepads ? navigator.getGamepads() : [];
      const p = pads && [...pads].find((x) => x);
      if (M && p) {
        const dz = setting(M, 'player.deadzone', 0.15);
        const ax = (v) => (Math.abs(v) < dz ? 0 : (v - Math.sign(v) * dz) / (1 - dz));
        const lx = ax(p.axes[0] || 0), ly = ax(p.axes[1] || 0), rx = ax(p.axes[2] || 0), ry = ax(p.axes[3] || 0);
        if (lx || ly || moving) { M._loop_move(lx, -ly); moving = !!(lx || ly); if (moving && onActive) onActive(); }
        if (rx || ry) M._loop_look(rx * 14, ry * 14);
        M._loop_key(4, p.buttons[4]?.pressed || p.buttons[5]?.pressed || p.buttons[10]?.pressed ? 1 : 0);
        const a = !!p.buttons[0]?.pressed;
        if (a && !wasA) onUse();
        wasA = a;
      }
      requestAnimationFrame(poll);
    }
    addEventListener('gamepadconnected', () => {});
    requestAnimationFrame(poll);
  }

  // story.hud and player.stickSize, and pausing while the page is hidden
  function applyPage(M, opts = {}) {
    const size = setting(M, 'player.stickSize', 110);
    document.querySelectorAll('.stick').forEach((s) => { s.style.width = s.style.height = size + 'px'; });
    if (opts.hud) opts.hud.forEach((el) => { el.style.display = setting(M, 'story.hud', true) ? '' : 'none'; });
  }
  function pauseWhenHidden(getM) {
    let pausedByUs = false;
    document.addEventListener('visibilitychange', () => {
      const M = getM();
      if (!M || !M._loop_set_option) return;
      if (document.hidden && setting(M, 'render.pauseHidden', true)) {
        const n = M.lengthBytesUTF8('pause') + 1, p = M._malloc(n);
        M.stringToUTF8('pause', p, n); M._loop_set_option(p, 1); M._free(p);
        pausedByUs = true;
      } else if (!document.hidden && pausedByUs) {
        const n = M.lengthBytesUTF8('pause') + 1, p = M._malloc(n);
        M.stringToUTF8('pause', p, n); M._loop_set_option(p, 0); M._free(p);
        pausedByUs = false;
      }
    });
  }

  // `use "name"` brings in a script from the same folder (once). Shared
  // scripts go first and the scene's own lines keep their numbers after them.
  async function assemble(text, fetchScene) {
    const seen = new Set(), pre = [], own = [];
    async function expand(t) {
      const out = [];
      for (const line of t.split('\n')) {
        const m = line.match(/^\s*use\s+"([^"]+)"/);
        if (!m) { out.push(line); continue; }
        if (!seen.has(m[1])) { seen.add(m[1]); try { out.push(await expand(await fetchScene(m[1]))); } catch (e) { out.push('say "can\'t find ' + m[1] + '.loop"'); } }
        out.push('');
      }
      return out.join('\n');
    }
    for (const line of text.split('\n')) {
      const m = line.match(/^\s*use\s+"([^"]+)"/);
      if (m && !seen.has(m[1])) {
        seen.add(m[1]);
        try { pre.push(await expand(await fetchScene(m[1]))); } catch (e) { pre.push('say "can\'t find ' + m[1] + '.loop"'); }
        own.push('');
      } else own.push(m ? '' : line);
    }
    const head = pre.length ? pre.join('\n') + '\n' : '';
    return { text: head + own.join('\n'), offset: head ? head.split('\n').length - 1 : 0 };
  }

  // What the editor hands to the player: the scene it has open, edits and all
  // (kept in this browser), so "Play" plays exactly what's in the editor.
  const HANDOFF = 'loop3d.play';
  function handTo(scene, text, project) {
    try { localStorage.setItem(HANDOFF, JSON.stringify({ scene, text, project, at: Date.now() })); return true; } catch (e) { return false; }
  }
  function handed() {
    try { return JSON.parse(localStorage.getItem(HANDOFF) || 'null'); } catch (e) { return null; }
  }

  return { setting, showLine, showAsk, loadingText, fader, keyMap, useKey, gamepad, applyPage, pauseWhenHidden, assemble, handTo, handed };
})();
