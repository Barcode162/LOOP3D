// Writes docs/SETTINGS.md from the editor's settings (web/editor/settings_schema.js)
// and the engine's (`loop settings list`): every setting, what it does, its
// range and default, how a script sets it, and whether it works yet.
//   node tools/settings_doc.js [path to the loop tool] > docs/SETTINGS.md
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.join(__dirname, '..');
const loop = process.argv[2] || path.join(root, 'build', 'loop');
const ctx = {};
ctx.window = ctx;
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(root, 'web/editor/settings_schema.js'), 'utf8'), ctx);
const engine = new Map(JSON.parse(execFileSync(loop, ['settings', 'list'], { encoding: 'utf8' })).map((e) => [e.id, e]));

const show = (v) => (Array.isArray(v) ? '(' + v.join(', ') + ')' : v === '' ? '""' : String(v));
const cell = (s) => String(s || '').replace(/\|/g, '\\|').replace(/\n/g, ' ');
function status(it) {
  if (it.bind === 'set') return 'live · script';
  if (it.bind === 'quality' || (it.bind && (it.bind.startsWith('look:') || it.bind.startsWith('world:')))) return 'live';
  if (it.bind === 'editor') return 'editor';
  if (it.bind === 'project' || it.bind === 'build') return 'project.json';
  return it.status === 'you' ? 'needs you' : 'not built yet';
}
function range(it) {
  const e = engine.get(it.id);
  if (it.options) return it.options.map((o) => show(o.value)).join(' / ');
  if (it.min !== undefined) return show(it.min) + ' … ' + show(it.max) + (it.unit ? ' ' + it.unit : '');
  if (e && e.min !== undefined) return show(e.min) + ' … ' + show(e.max);
  return '';
}
function how(it) {
  if (it.bind === 'set') return '`setting "' + it.id + '", ' + JSON.stringify(it.def) + '`' + (it.word ? ' · `' + it.word + '`' : '');
  return it.word ? '`' + it.word + '`' : '';
}

let out = `# Every setting

Made by \`node tools/settings_doc.js > docs/SETTINGS.md\` from the editor's settings
(\`web/editor/settings_schema.js\`) and the engine's (\`loop settings list\`): don't edit by hand.

## How settings work

An engine setting has one name everywhere: in the editor (Project Settings), in a project's
\`project.json\` and in a scene's script. Values come in three layers, the higher winning:

1. **The editor**: what you change while editing (kept in your browser).
2. **The scene**: what its script asks for, with \`setting "physics.iterations", 24\`,
   \`settings {story_textSpeed: 20, "audio.reverb": 1.5}\` (\`_\` or \`.\` between the parts), or a world word
   (\`gravity(…)\`, \`look(…)\` …). A new scene starts from the project's values again.
3. **The project**: \`projects/<name>/project.json\`, \`"settings": {"render.lamps": 4}\` (the editor writes it:
   File → Save to project). \`loop render\` and the site build read it too.

\`get_setting("player.walk")\` reads a setting's value in effect.

Statuses: **live** changes the picture or the world as you set it (**· script**: a scene can set it too);
**editor** is the editor's own; **project.json** is read by the site build; **not built yet** is a feature on the
roadmap (docs/TUNING.md); **needs you** is your call.

`;
let counts = {};
for (const scope of ['project', 'editor']) {
  out += scope === 'project' ? '\n# Project settings\n' : '\n# Editor preferences\n\nKept per browser; they change the editor, not the game.\n';
  for (const cat of ctx.LE.SCHEMA[scope]) {
    out += `\n## ${cat.title}\n`;
    for (const g of cat.groups) {
      out += `\n### ${g.title}\n\n| Setting | Name | Default | Range | Status | In a script | What it does |\n|---|---|---|---|---|---|---|\n`;
      for (const it of g.items) {
        const st = status(it);
        counts[st] = (counts[st] || 0) + 1;
        const e = engine.get(it.id);
        const help = it.help || (e && e.help) || '';
        out += `| ${cell(it.label)} | \`${it.id}\` | ${cell(show(it.def))} | ${cell(range(it))} | ${st} | ${cell(how(it))} | ${cell(help)} |\n`;
      }
    }
  }
}
out += '\n---\n\n' + Object.entries(counts).map(([k, v]) => `${v} ${k}`).join(' · ') + '\n';
process.stdout.write(out);
