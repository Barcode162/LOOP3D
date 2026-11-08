// The live recorder (web/loop_ui.js: recorder, recordingStore, summary), with a
// pretend engine and a pretend claude.ai database: `node tests/recorder_test.js`.
global.window = {};
const L = require('../web/loop_ui.js');
let failed = 0, checks = 0;
const eq = (got, want, what) => {
  checks++;
  if (JSON.stringify(got) !== JSON.stringify(want)) { failed++; console.log('FAIL ' + what + '\n  got:  ' + JSON.stringify(got) + '\n  want: ' + JSON.stringify(want)); }
};
const tick = () => new Promise((r) => setTimeout(r, 0));

// the engine's timing calls: frames add up between parts; perf_part hands them over
function fakeEngine() {
  const e = { calls: [], frames: 0, on: false };
  e.M = {
    UTF8ToString: (s) => s,
    _loop_perf_begin() { e.calls.push('begin'); e.on = true; e.frames = 0; },
    _loop_perf_go() { e.calls.push('go'); e.on = true; e.frames = 0; },
    _loop_perf_end() { e.calls.push('end'); e.on = false; return '{}'; },
    _loop_perf_part() {
      const f = e.frames; e.frames = 0;
      return JSON.stringify({ title: 'T', frames: f, seconds: f / 60, fps: f ? 60 : 0, low1Fps: 50, p99Ms: 20, maxMs: 30, hitches: 1, scaleAvg: 1, loadMs: 100, gpuMs: 9 });
    },
  };
  return e;
}
function fakeStore() { const s = { saved: [] }; s.save = (visit, id) => s.saved.push({ id, parts: visit.segments.length }); return s; }

// ---- the recorder
{
  const e = fakeEngine(), store = fakeStore();
  let q = 'Phone', t = 0;
  const rec = L.recorder({ getM: () => e.M, quality: () => q, store, build: 'abc', partMs: 1e9, clock: () => new Date(Date.UTC(2026, 10, 8, 12, 0, t++)) });
  eq(rec.cut(), false, 'nothing to cut before a scene');
  eq(rec.id.startsWith('live-20261108-120000-'), true, 'the visit is named by its start');
  rec.scene('bar');
  eq(e.calls, ['begin'], 'a scene starts its recording at its first frame');
  e.frames = 1800;
  eq(rec.cut(), true, 'a part is cut');
  eq(rec.visit.segments.map((s) => [s.scene, s.part, s.quality, s.frames]), [['bar', 0, 'Phone', 1800]], 'the part names its scene, number and quality');
  eq(store.saved.length, 1, 'each part is saved as it ends');
  eq(rec.cut(), false, 'a part with no frames is not kept');
  e.frames = 600; q = 'High'; rec.quality();
  e.frames = 300; rec.cut();
  eq(rec.visit.segments.map((s) => s.quality), ['Phone', 'Phone', 'High'], 'a quality change ends the part under the old name');
  e.frames = 120; rec.scene('the_woods');
  eq(rec.visit.segments.map((s) => [s.scene, s.part]), [['bar', 0], ['bar', 1], ['bar', 2], ['bar', 3]], 'the next scene cuts the last part of the one before, under its own name');
  eq(e.calls.slice(-1), ['begin'], 'and starts its own recording');
  e.frames = 900; rec.cut();
  eq([rec.visit.segments[4].scene, rec.visit.segments[4].part], ['the_woods', 0], 'parts count again from 0 in a new scene');
  eq(rec.visit.start, 'bar', 'the visit keeps where it started');
  e.frames = 60; rec.hidden();
  eq(e.calls.slice(-1), ['end'], 'in the background the recording stops');
  eq(rec.visit.segments.length, 6, 'after keeping what came before');
  eq(rec.cut(), false, 'nothing is cut while away');
  rec.shown();
  eq(e.calls.slice(-1), ['go'], 'back again, it records at once (the time away is no frame)');
  eq(rec.visit.summary.scenes, 2, 'the summary counts scenes');
  eq(rec.visit.summary.parts, 6, 'and parts');
  eq(rec.visit.bench, 'live', 'a live recording says so');
  rec.stop();
}

// ---- what went wrong is kept, and saved at once
{
  const e = fakeEngine(), store = fakeStore();
  const rec = L.recorder({ getM: () => e.M, quality: () => 'Phone', store, partMs: 1e9, slowMs: 30 });
  rec.event('no-webgpu', 'Safari');
  eq([rec.visit.events.length, rec.visit.events[0].kind, rec.visit.events[0].text, store.saved.length], [1, 'no-webgpu', 'Safari', 1], 'an event is kept and saved at once, with no parts yet');
  rec.scene('bar'); rec.stage('fetching models 12/167');
  rec.hidden();
  eq(rec.visit.events.slice(-1).map((x) => [x.kind, x.scene, x.stage]), [['left-while-loading', 'bar', 'fetching models 12/167']], 'leaving while a scene loads says how far it got');
  rec.shown();
  global.slowRec = rec;
}
{
  const e = fakeEngine(), store = fakeStore();
  const rec = L.recorder({ getM: () => e.M, quality: () => 'Phone', store, partMs: 1e9, slowMs: 30 });
  rec.scene('courtyard'); rec.firstFrame();
  global.fastRec = rec;
}

// ---- the summary (the benchmark's too)
{
  const s = L.summary([{ scene: 'a', frames: 600, seconds: 10, low1Fps: 40, p99Ms: 25, maxMs: 40, hitches: 2, scaleAvg: 1, loadMs: 300 },
                       { scene: 'b', frames: 300, seconds: 10, low1Fps: 20, p99Ms: 60, maxMs: 90, hitches: 5, scaleAvg: 0.5, loadMs: -1 }]);
  eq([s.scenes, s.frames, s.fps, s.worstLow1Fps, s.worstP99Ms, s.maxMs, s.hitches, s.avgScale, s.worstLoadMs], [2, 900, 45, 20, 60, 90, 7, 0.75, 300], 'summary');
  eq(L.summary([]).fps, 0, 'an empty summary');
}

// ---- the store
function fakeWin(db) {
  const w = { localStorage: { data: {}, setItem(k, v) { this.data[k] = v; } } };
  if (db) w.claude = { use: (name) => Promise.resolve(name === 'db' ? db : null) };
  return w;
}
function fakeDb(answers) {
  const d = { writes: [], waiting: [] };
  d.doc = (path) => ({
    set(body) {
      d.writes.push({ path, parts: body.segments.length, start: body.start, id: body.id, summaryParts: body.summary.parts });
      const a = (answers || []).shift();
      return a ? Promise.reject({ code: a }) : new Promise((r) => d.waiting.push(r));
    },
  });
  d.answer = () => { const r = d.waiting.shift(); if (r) r(); };
  return d;
}
const visitOf = (n, id) => ({ kind: 'loop3d-bench', bench: 'live', start: 's0', segments: Array.from({ length: n }, (_, i) => ({ scene: 's' + i, frames: 60, seconds: 1, low1Fps: 60, p99Ms: 17, maxMs: 17, hitches: 0, scaleAvg: 1, loadMs: -1 })) });

(async () => {
  {
    const w = fakeWin(null), store = L.recordingStore(w);
    store.save(visitOf(1), 'v1');
    await tick();
    eq(store.toClaude, false, 'outside claude.ai nothing goes to a database');
    eq(JSON.parse(w.localStorage.data['loop3d.live']).segments.length, 1, 'the visit stays in this browser (Copy my recording)');
  }
  {
    const db = fakeDb(), w = fakeWin(db), seen = [];
    const store = L.recordingStore(w, (v, ok) => seen.push(ok));
    await tick();
    eq(store.toClaude, true, 'on the recording page the visit goes to its database');
    store.save(visitOf(1), 'v1');
    store.save(visitOf(2), 'v1');
    store.save(visitOf(3), 'v1');
    await tick();
    eq(db.writes.map((x) => [x.path, x.parts]), [['runs/v1', 1]], 'one write at a time');
    db.answer(); await tick(); await tick(); await tick();
    eq(db.writes.map((x) => [x.path, x.parts]), [['runs/v1', 1], ['runs/v1', 3]], 'then the latest, skipping the ones in between');
    db.answer(); await tick();
    eq(seen, [true, true], 'each write is reported');
    store.save(visitOf(151), 'v1');
    await tick();
    eq(db.writes.slice(-1).map((x) => [x.path, x.parts, x.start, x.id, x.summaryParts]), [['runs/v1-2', 1, 's150', 'v1-2', 1]], 'past 150 parts a visit goes on in its next row, a run of its own');
    db.answer(); await tick();
  }
  {
    const db = fakeDb(['invalid_argument']), seen = [];
    const store = L.recordingStore(fakeWin(db), (v, ok, code) => seen.push([ok, code]));
    await tick();
    store.save(visitOf(1), 'v2');
    await tick(); await tick();
    store.save(visitOf(2), 'v2');
    await tick();
    eq(seen, [[false, 'invalid_argument']], 'a refused write is reported');
    eq(db.writes.length, 1, 'and no more are tried');
  }
  {
    const db = fakeDb(['unavailable']);
    const store = L.recordingStore(fakeWin(db));
    await tick();
    store.save(visitOf(1), 'v3');
    await new Promise((r) => setTimeout(r, 2200));
    eq(db.writes.map((x) => x.path), ['runs/v3', 'runs/v3'], 'a passing hiccup is tried once more');
    db.answer();
  }
  // a scene with no picture after the wait says so (twice: at 1x and 3x the wait); one that showed says nothing
  await new Promise((r) => setTimeout(r, 120));
  eq(global.slowRec.visit.events.filter((x) => x.kind === 'slow-load').map((x) => [x.scene, x.stage]), [['bar', 'fetching models 12/167'], ['bar', 'fetching models 12/167']], 'a scene that shows no picture is reported with its stage');
  eq(global.fastRec.visit.events.length, 0, 'a scene that showed its picture reports nothing');
  global.slowRec.stop(); global.fastRec.stop();
  {
    const db = fakeDb(), store = L.recordingStore(fakeWin(db));
    await tick();
    store.save({ ...visitOf(0), events: [{ kind: 'gpu-lost' }] }, 'v4');
    await tick();
    eq(db.writes.map((x) => [x.path, x.parts]), [['runs/v4', 0]], 'a visit with only an event is written');
    db.answer();
  }
  console.log(`${checks} recorder checks, ${failed} failed`);
  process.exit(failed ? 1 : 0);
})();
