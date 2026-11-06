// The editor's script edits (web/editor/source.js): run with `node tests/editor_source_test.js`.
// Every rewritten line is also checked by the engine itself when `build/loop` exists.
const S = require('../web/editor/source.js');
const fs = require('fs'), os = require('os'), path = require('path'), { execFileSync } = require('child_process');
let failed = 0, checks = 0;
const eq = (got, want, what) => {
  checks++;
  if (JSON.stringify(got) !== JSON.stringify(want)) { failed++; console.log('FAIL ' + what + '\n  got:  ' + JSON.stringify(got) + '\n  want: ' + JSON.stringify(want)); }
};

// numbers as people write them
eq(S.num(1.5), '1.5', 'num 1.5'); eq(S.num(2), '2', 'num 2'); eq(S.num(-0.0001), '0', 'no -0'); eq(S.num(0.12345), '0.123', 'num rounds');

// finding a setting's value at the call's own level
const box = 'box size: vec(1, 2, 3), at: vec(0, 1, 0), material: material("wool", color: rgb(1, 0, 0))  # a crate';
eq(S.getArg(box, 'at'), 'vec(0, 1, 0)', 'at found');
eq(S.getArg(box, 'color'), null, 'nested settings are not the call\'s');
eq(S.getArg('let c = box(size: vec(1, 1, 1), at: vec(2, 0, 0))', 'at'), 'vec(2, 0, 0)', 'inside parentheses');
eq(S.getArg('shot "a", 3 s, from: {at: vec(1, 2, 3)}', 'at'), null, 'a shot\'s from: {at} is not its at');

// moving: a plain vec is replaced, the comment kept
eq(S.moveLine(box, [1, 1.25, -2], [1, 0.25, -2], false), 'box size: vec(1, 2, 3), at: vec(1, 1.25, -2), material: material("wool", color: rgb(1, 0, 0))  # a crate', 'move plain');
eq(S.moveLine('sphere 0.5', [0, 0.5, 1], [0, 0.5, 1], false), 'sphere 0.5, at: vec(0, 0.5, 1)', 'move adds at');
eq(S.moveLine('let b = sphere(0.5)', [1, 0, 0], [1, 0, 0], false), 'let b = sphere(0.5, at: vec(1, 0, 0))', 'move adds at in parentheses');
eq(S.moveLine('cube()', [1, 0, 0], [1, 0, 0], false), 'cube(at: vec(1, 0, 0))', 'first setting in empty parentheses');
// a repeat or an expression: an offset is added, and folds into the same offset next time
const rep = '  cylinder 0.2, 1, at: vec(-3 - i, 0.5, 0)';
const r1 = S.moveLine(rep, null, [0.5, 0, 0], true);
eq(r1, '  cylinder 0.2, 1, at: (vec(-3 - i, 0.5, 0)) + vec(0.5, 0, 0)', 'repeat gets an offset');
eq(S.moveLine(r1, null, [-0.5, 0, 0], true), rep, 'offsets that cancel go away');
eq(S.moveLine('box at: corner, size: vec(1, 1, 1)', [0, 0, 0], [0, 1, 0], false), 'box at: (corner) + vec(0, 1, 0), size: vec(1, 1, 1)', 'a variable gets an offset');

// turning
eq(S.turnLine('box size: vec(1, 1, 1)', Math.PI / 4, Math.PI / 4, false), 'box size: vec(1, 1, 1), turn: 45 deg', 'turn added');
eq(S.turnLine('model "a.glb", turn: 30 deg', Math.PI / 2, Math.PI / 3, false), 'model "a.glb", turn: 90 deg', 'turn replaced');
eq(S.turnLine('box rotation: vec(0, 0, 90 deg)', Math.PI / 2, Math.PI / 2, false), 'box rotation: vec(90 deg, 0 deg, 90 deg)', 'rotation\'s yaw');
eq(S.turnLine('  log turn: i * 10 deg', 0, Math.PI / 18, true), '  log turn: (i * 10 deg) + 10 deg', 'turn offset');

// sizing, and back to normal size removes the setting
eq(S.scaleLine('sphere 1, at: vec(0, 1, 0)', [2, 2, 2]), 'sphere 1, at: vec(0, 1, 0), scale: vec(2, 2, 2)', 'scale added');
eq(S.scaleLine('sphere 1, scale: vec(2, 2, 2), at: vec(0, 1, 0)', [1, 1, 1]), 'sphere 1, at: vec(0, 1, 0)', 'scale 1 removed');
eq(S.removeArg('box at: vec(0, 0, 0), size: vec(1, 1, 1)', 'at'), 'box size: vec(1, 1, 1)', 'remove the first setting');

// whole-text edits
const text = 'title "T"\nlet crate = box(size: vec(1, 1, 1), at: vec(0, 0.5, 0))\nsphere 0.5, at: vec(2, 0.5, 0)\ncrate.turn = 10 deg\n';
eq(S.removeLine(text, 2).refused, [4], 'removing a used name is refused');
eq(S.removeLine(text, 3).text, 'title "T"\nlet crate = box(size: vec(1, 1, 1), at: vec(0, 0.5, 0))\ncrate.turn = 10 deg\n', 'remove a line');
const dup = S.duplicateLine(text, 2, [1, 0.5, 0], [1, 0, 0], false);
eq(S.lineAt(dup.text, 3), 'let crate_2 = box(size: vec(1, 1, 1), at: vec(1, 0.5, 0))', 'duplicate renames a let');
eq(dup.line, 3, 'duplicate\'s line');
eq(S.insertAfter('a\nb\n', 9, 'c'), 'a\nb\nc\n', 'insert at the end keeps the last newline');
eq(S.shotLines('title "x"\nshot "a", 2 s\norbit "b", 3 s\n'), [2, 3], 'shot lines');
eq(S.soundLines('sound "fire", at: vec(1, 0, 2), volume: 0.5\n'), [{ line: 1, kind: 'fire', at: [1, 0, 2], volume: 0.5 }], 'sound lines');
eq(S.setStatement('title "x"\nmode "game"\nbox\n', 'look', '"noir"'), 'title "x"\nmode "game"\nlook "noir"\nbox\n', 'look added after mode');
eq(S.setStatement('look "golden"\n', 'look', '"noir"'), 'look "noir"\n', 'look replaced');

// the engine reads what the editor writes
const loop = path.join(__dirname, '..', 'build', 'loop');
if (fs.existsSync(loop)) {
  const script = ['title "Edited"', S.moveLine('box size: vec(1, 1, 1)', [1, 0.5, 0], [1, 0.5, 0], false),
    'for i from 0 to 2', S.moveLine(rep, null, [0.5, 0, 0], true), S.turnLine('  log turn: i * 10 deg, length: 1', 0, Math.PI / 18, true),
    'let corner = vec(1, 0, 1)', S.moveLine('box at: corner, size: vec(1, 1, 1)', [0, 0, 0], [0, 1, 0], false),
    S.lineAt(dup.text, 3), S.turnLine('box rotation: vec(0, 0, 90 deg)', Math.PI / 2, Math.PI / 2, false), ''].join('\n');
  const f = path.join(os.tmpdir(), 'editor_source_test.loop');
  fs.writeFileSync(f, script);
  checks++;
  try { execFileSync(loop, ['check', f], { stdio: 'pipe' }); } catch (e) { failed++; console.log('FAIL the engine refuses the edited script:\n' + script + '\n' + String(e.stdout) + String(e.stderr)); }
}

console.log(checks + ' editor source checks, ' + failed + ' failed');
process.exit(failed ? 1 : 0);
