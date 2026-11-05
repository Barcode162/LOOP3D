// Checks that the editor's settings (web/editor/settings_schema.js) and the
// engine's (engine/core/settings.*, as `loop settings list` prints them) agree:
//   node tools/check_settings.js [path to the loop tool]
// Fails if a setting the editor sends to the engine isn't one, if an engine
// setting has no place in the editor, or if their defaults differ.
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.join(__dirname, '..');
const loop = process.argv[2] || path.join(root, 'build', 'loop');
const ctx = { window: {} };
ctx.window = ctx;
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(root, 'web/editor/settings_schema.js'), 'utf8'), ctx);
const engine = JSON.parse(execFileSync(loop, ['settings', 'list'], { encoding: 'utf8' }));
const byId = new Map(engine.map((e) => [e.id, e]));

const problems = [];
const editorIds = new Set();
let live = 0, nb = 0, you = 0, total = 0;
for (const scope of ['project', 'editor'])
  for (const cat of ctx.LE.SCHEMA[scope])
    for (const g of cat.groups)
      for (const it of g.items) {
        total++;
        if (it.bind) live++;
        else if (it.status === 'you') you++;
        else nb++;
        if (it.bind !== 'set') continue;
        editorIds.add(it.id);
        const e = byId.get(it.id);
        if (!e) { problems.push(`${it.id}: the editor sends it to the engine, but the engine has no such setting`); continue; }
        const same = (a, b) => (Array.isArray(a) ? a.every((x, i) => Math.abs(x - b[i]) < 1e-3) : typeof a === 'number' ? Math.abs(a - b) < 0.01 + Math.abs(a) * 0.03 : a === b);
        if (!same(e.default, it.def)) problems.push(`${it.id}: default ${JSON.stringify(it.def)} in the editor, ${JSON.stringify(e.default)} in the engine`);
      }
for (const e of engine) if (!editorIds.has(e.id)) problems.push(`${e.id}: an engine setting the editor doesn't show`);

console.log(`${total} editor settings: ${live} working, ${nb} not built yet, ${you} need you; ${engine.length} engine settings`);
if (problems.length) {
  for (const p of problems) console.log('  problem: ' + p);
  process.exit(1);
}
console.log('the editor and the engine agree');
