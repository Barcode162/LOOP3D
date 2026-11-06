// Nothing is ever lost. Each scene's script is kept in this browser the
// moment it changes; undo and redo survive a reload; and every change is a
// version you can go back to (the newest 200 per scene). The script text is
// what's kept, so it can go to the repo as it is.
//   localStorage  loop3d.script.<scene>   the scene's text now
//   IndexedDB     loop3d-editor           undo/redo stacks and versions
window.LE = window.LE || {};
(() => {
  const MAX_UNDO = 120, MAX_VERSIONS = 200;
  let db = null;
  const dbReady = new Promise((resolve) => {
    try {
      const rq = indexedDB.open('loop3d-editor', 1);
      rq.onupgradeneeded = () => { rq.result.createObjectStore('stacks'); rq.result.createObjectStore('versions'); };
      rq.onsuccess = () => { db = rq.result; resolve(db); };
      rq.onerror = () => resolve(null);
    } catch (e) { resolve(null); }
  });
  const idb = (store, mode, fn) => dbReady.then((d) => new Promise((resolve) => {
    if (!d) return resolve(null);
    try {
      const tx = d.transaction(store, mode), st = tx.objectStore(store);
      const rq = fn(st);
      tx.oncomplete = () => resolve(rq ? rq.result : null);
      tx.onerror = () => resolve(null);
    } catch (e) { resolve(null); }
  }));
  const local = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } },
    del(k) { try { localStorage.removeItem(k); } catch (e) { /* private mode */ } },
  };

  const listeners = [];
  const H = {
    scene: null, fileText: '', text: '', undo: [], redo: [], versions: [],
    on(fn) { listeners.push(fn); },
    emit(kind) { listeners.forEach((fn) => { try { fn(kind, H); } catch (e) { console.error(e); } }); },
    edited() { return H.text !== H.fileText; },

    // Opens a scene: its kept text (if any) over the file's, and its stacks.
    async open(scene, fileText) {
      H.scene = scene;
      H.fileText = fileText;
      const kept = local.get('loop3d.script.' + scene);
      H.text = kept !== null ? kept : fileText;
      H.undo = []; H.redo = []; H.versions = [];
      const st = await idb('stacks', 'readonly', (s) => s.get(scene));
      if (st && H.scene === scene && st.text === H.text) { H.undo = st.undo || []; H.redo = st.redo || []; }
      const vs = await idb('versions', 'readonly', (s) => s.get(scene));
      if (vs && H.scene === scene) H.versions = vs;
      H.emit('open');
      return H.text;
    },
    // A change: the text before goes on the undo stack. `fast` says how to
    // undo it without running the script again (things moved: before and after).
    commit(newText, label, fast) {
      if (newText === H.text) return false;
      H.undo.push({ text: H.text, label, fast: fast || null, time: Date.now() });
      if (H.undo.length > MAX_UNDO) H.undo.shift();
      H.redo = [];
      H.text = newText;
      H.versions.unshift({ text: newText, label, time: Date.now() });
      if (H.versions.length > MAX_VERSIONS) H.versions.length = MAX_VERSIONS;
      H.save(true);
      H.emit('commit');
      return true;
    },
    // Undo: gives back the entry ({text, label, fast}) so the editor can show it.
    undoOnce() {
      const e = H.undo.pop();
      if (!e) return null;
      H.redo.push({ text: H.text, label: e.label, fast: e.fast, time: Date.now() });
      H.text = e.text;
      H.save(false);
      H.emit('undo');
      return { ...e, dir: 'undo' };
    },
    redoOnce() {
      const e = H.redo.pop();
      if (!e) return null;
      H.undo.push({ text: H.text, label: e.label, fast: e.fast, time: Date.now() });
      H.text = e.text;
      H.save(false);
      H.emit('redo');
      return { ...e, dir: 'redo' };
    },
    save(version) {
      if (!H.scene) return;
      const scene = H.scene;
      if (H.text === H.fileText) local.del('loop3d.script.' + scene);
      else if (!local.set('loop3d.script.' + scene, H.text)) LE.toast && LE.toast('This browser is out of room: copy your script somewhere safe (Logic → Copy).', 'err');
      const snap = { text: H.text, undo: H.undo, redo: H.redo };
      idb('stacks', 'readwrite', (s) => s.put(snap, scene));
      if (version) { const vs = H.versions.slice(); idb('versions', 'readwrite', (s) => s.put(vs, scene)); }
      H.emit('saved');
    },
    // Back to the file as it is on the site (itself a change you can undo).
    revert() { return H.commit(H.fileText, 'back to the file'); },
  };
  LE.history = H;
})();
