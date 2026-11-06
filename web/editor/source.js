// Text is the source: the editor changes a world by changing its script.
// These are plain functions on the script's text (no engine, no page), so
// they're tested on their own (tests/editor_source_test.js):
//   • a thing's named setting on its line (at:, turn:, scale:, size:, material:)
//   • a thing made by a repeat moves by an offset added to its expression
//   • lines added, duplicated (a `let` gets a new name) and removed (refused
//     while another line still uses its name)
(function (root) {
  // A number as a person would write it: 3 decimals at most, no "-0", no "1.500".
  function num(n) {
    if (!isFinite(n)) return '0';
    let s = (Math.round(n * 1000) / 1000).toFixed(3).replace(/\.?0+$/, '');
    if (s === '-0') s = '0';
    return s;
  }
  const vec = (v) => 'vec(' + num(v[0]) + ', ' + num(v[1]) + ', ' + num(v[2]) + ')';
  const deg = (rad) => num(rad * 180 / Math.PI) + ' deg';

  // The parts of a line outside strings and its comment: depth of each
  // character in (), [], {} and where the comment starts.
  function scan(line) {
    const depth = new Array(line.length).fill(0);
    let d = 0, inStr = false, comment = line.length;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (inStr) {
        depth[i] = d;
        if (c === '\\') { depth[i + 1] = d; i++; continue; }
        if (c === '"') inStr = false;
        continue;
      }
      if (c === '"') { inStr = true; depth[i] = d; continue; }
      if (c === '#') { comment = i; break; }
      if (c === '(' || c === '[' || c === '{') { depth[i] = d; d++; continue; }
      if (c === ')' || c === ']' || c === '}') { d = Math.max(0, d - 1); depth[i] = d; continue; }
      depth[i] = d;
    }
    for (let i = comment; i < line.length; i++) depth[i] = -1;
    return { depth, comment };
  }

  // What a line makes: `box size: …` (the call's settings at depth 0) or
  // `let crate = box(…)` (inside the parentheses, depth 1).
  function callOf(line) {
    const m = line.match(/^(\s*)(?:let\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*(\()?/);
    if (!m) return null;
    return { indent: m[1], name: m[2] || null, word: m[3], paren: !!m[4], argsAt: m[0].length, depth: m[4] ? 1 : 0 };
  }

  // Where a named setting's value is on the line: {start, end} of the text
  // after "key:", or null.
  function argSpan(line, key) {
    const call = callOf(line);
    if (!call) return null;
    const { depth, comment } = scan(line);
    const re = new RegExp('(^|[\\s,(])' + key + '\\s*:', 'g');
    let m;
    while ((m = re.exec(line))) {
      const at = m.index + m[1].length;
      if (at < call.argsAt || at >= comment || depth[at] !== call.depth) continue;
      let start = m.index + m[0].length;
      while (line[start] === ' ') start++;
      let end = start;
      while (end < comment) {
        const c = line[end];
        if ((depth[end] === call.depth && c === ',') || (call.paren && c === ')' && depth[end] === call.depth - 1)) break;
        end++;
      }
      while (end > start && line[end - 1] === ' ') end--;
      return { start, end };
    }
    return null;
  }
  const getArg = (line, key) => { const s = argSpan(line, key); return s ? line.slice(s.start, s.end) : null; };

  // Sets a named setting on a line: replaces its value, or adds it at the end
  // of the call (before the closing parenthesis, or before a comment).
  function setArg(line, key, value) {
    const s = argSpan(line, key);
    if (s) return line.slice(0, s.start) + value + line.slice(s.end);
    const call = callOf(line);
    if (!call) return line;
    const { depth, comment } = scan(line);
    let end = comment;
    if (call.paren) {
      // the call's own closing parenthesis
      for (let i = call.argsAt; i < comment; i++) if (line[i] === ')' && depth[i] === 0) { end = i; break; }
    }
    let head = line.slice(0, end), tail = line.slice(end);
    const trimmed = head.replace(/\s+$/, '');
    const ws = head.slice(trimmed.length);
    const hasArgs = trimmed.length > call.argsAt || (call.paren && trimmed.length > call.argsAt);
    const sep = call.paren ? (trimmed.endsWith('(') ? '' : ', ') : (hasArgs ? ', ' : ' ');
    return trimmed + sep + key + ': ' + value + (call.paren ? '' : ws) + tail;
  }

  // A plain vec(x, y, z) of numbers, or null if the value is an expression.
  function vecLiteral(text) {
    if (text == null) return null;
    const m = text.match(/^vec\(\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\)$/);
    return m ? [+m[1], +m[2], +m[3]] : null;
  }
  // "expr + vec(a, b, c)": the expression and its offset.
  function splitOffset(text) {
    const m = text.match(/^\((.*)\)\s*\+\s*(vec\([^()]*\))$/);
    if (m && vecLiteral(m[2])) return { base: m[1], off: vecLiteral(m[2]) };
    return { base: text, off: [0, 0, 0] };
  }

  // Where a thing is now, written into its line. `delta` is how far it moved
  // (used when the line's at: is an expression or the line makes many things).
  function moveLine(line, at, delta, many) {
    const cur = getArg(line, 'at');
    if (!many && (cur === null || vecLiteral(cur))) return setArg(line, 'at', vec(at));
    if (cur === null) return setArg(line, 'at', vec(delta));  // many things at the origin: all move by delta
    const { base, off } = splitOffset(cur);
    const o = [off[0] + delta[0], off[1] + delta[1], off[2] + delta[2]];
    if (Math.abs(o[0]) < 5e-4 && Math.abs(o[1]) < 5e-4 && Math.abs(o[2]) < 5e-4) return setArg(line, 'at', base);
    return setArg(line, 'at', '(' + base + ') + ' + vec(o));
  }
  // A turn (radians, about the up axis) written as `turn: N deg`; an
  // expression keeps its value and gets the difference added.
  function turnLine(line, turn, deltaTurn, many) {
    const rot = getArg(line, 'rotation');
    const lit = vecLiteral(rot ? rot.replace(/\s*deg\b/g, '') : null);
    if (rot !== null && lit) {
      // rotation: vec(yaw, pitch, roll) in degrees
      return setArg(line, 'rotation', 'vec(' + deg(turn) + ', ' + num(lit[1]) + ' deg, ' + num(lit[2]) + ' deg)');
    }
    const cur = getArg(line, 'turn');
    const plain = cur !== null && /^-?[\d.]+\s*(deg|degrees)?$/.test(cur);
    if (!many && (cur === null || plain)) return setArg(line, 'turn', deg(turn));
    if (cur === null) return setArg(line, 'turn', deg(deltaTurn));
    const m = cur.match(/^\((.*)\)\s*\+\s*(-?[\d.]+)\s*deg$/);
    const base = m ? m[1] : cur, off = (m ? +m[2] : 0) + deltaTurn * 180 / Math.PI;
    if (Math.abs(off) < 0.05) return setArg(line, 'turn', base);
    return setArg(line, 'turn', '(' + base + ') + ' + num(off) + ' deg');
  }
  function scaleLine(line, scale) {
    const same = Math.abs(scale[0] - scale[1]) < 1e-4 && Math.abs(scale[1] - scale[2]) < 1e-4;
    if (same && Math.abs(scale[0] - 1) < 1e-4 && getArg(line, 'scale') !== null) return removeArg(line, 'scale');
    return setArg(line, 'scale', vec(scale));
  }
  function removeArg(line, key) {
    const s = argSpan(line, key);
    if (!s) return line;
    // from the comma before the key to the value's end
    let from = line.lastIndexOf(key, s.start);
    let j = from - 1;
    while (j >= 0 && line[j] === ' ') j--;
    if (line[j] === ',') from = j;
    else {
      // the first setting: take the comma after it instead
      let k = s.end;
      while (line[k] === ' ') k++;
      if (line[k] === ',') { k++; while (line[k] === ' ') k++; return line.slice(0, from) + line.slice(k); }
    }
    return line.slice(0, from) + line.slice(s.end);
  }

  // ---- whole-text edits (lines are 1-based, like the engine's)
  const lines = (text) => text.split('\n');
  function replaceLine(text, n, newLine) {
    const L = lines(text);
    if (n < 1 || n > L.length) return text;
    L[n - 1] = newLine;
    return L.join('\n');
  }
  const lineAt = (text, n) => lines(text)[n - 1] ?? '';
  // Adds lines after line n (0: at the very start; past the end: at the end).
  function insertAfter(text, n, newLines) {
    const L = lines(text);
    const at = Math.max(0, Math.min(n, L.length));
    const add = Array.isArray(newLines) ? newLines : [newLines];
    // keep a final empty line last
    if (at === L.length && L[L.length - 1] === '') { L.splice(L.length - 1, 0, ...add); return L.join('\n'); }
    L.splice(at, 0, ...add);
    return L.join('\n');
  }
  // Where the names of a text are used, besides their own `let` line.
  function usesOf(text, name, ownLine) {
    const re = new RegExp('(^|[^A-Za-z0-9_."])' + name + '(?![A-Za-z0-9_])');
    const out = [];
    lines(text).forEach((l, i) => {
      if (i + 1 === ownLine) return;
      const code = l.slice(0, scan(l).comment);
      if (re.test(code.replace(/"(?:[^"\\]|\\.)*"/g, '""'))) out.push(i + 1);
    });
    return out;
  }
  // Removes a thing's line; refused (with the lines that use it) while its
  // name is still used.
  function removeLine(text, n) {
    const call = callOf(lineAt(text, n));
    if (call && call.name) {
      const used = usesOf(text, call.name, n);
      if (used.length) return { text, refused: used, name: call.name };
    }
    const L = lines(text);
    L.splice(n - 1, 1);
    return { text: L.join('\n') };
  }
  // A copy of a thing's line right after it, moved by `offset`; a `let` gets a
  // fresh name (crate → crate_2) so the script stays right.
  function duplicateLine(text, n, at, offsetDelta, many) {
    let line = lineAt(text, n);
    const call = callOf(line);
    if (call && call.name) {
      let k = 2, fresh;
      do fresh = call.name.replace(/_\d+$/, '') + '_' + k++; while (new RegExp('\\b' + fresh + '\\b').test(text));
      line = line.replace(new RegExp('^(\\s*let\\s+)' + call.name + '\\b'), '$1' + fresh);
    }
    line = moveLine(line, at, offsetDelta, many);
    return { text: insertAfter(text, n, line), line: n + 1 };
  }

  // The text's shots in order, with their lines: `shot "name", 3 s, …` and `orbit …`.
  function shotLines(text) {
    const out = [];
    lines(text).forEach((l, i) => { const c = callOf(l); if (c && (c.word === 'shot' || c.word === 'orbit') && !/^\s/.test(l)) out.push(i + 1); });
    return out;
  }
  // The text's sounds: {line, kind, at, volume}.
  function soundLines(text) {
    const out = [];
    lines(text).forEach((l, i) => {
      const c = callOf(l);
      if (!c || c.word !== 'sound') return;
      const kind = (l.slice(c.argsAt).match(/^\s*"([^"]+)"/) || [])[1] || '';
      out.push({ line: i + 1, kind, at: vecLiteral(getArg(l, 'at')), volume: +(getArg(l, 'volume') ?? 1) });
    });
    return out;
  }
  // Sets (or adds) a one-word line like `look "noir"` or `title "…"`.
  function setStatement(text, word, valueText) {
    const L = lines(text);
    const i = L.findIndex((l) => new RegExp('^' + word + '\\b').test(l));
    const line = word + ' ' + valueText;
    if (i >= 0) { L[i] = line; return L.join('\n'); }
    // after the title and mode lines, or at the start
    let at = 0;
    L.forEach((l, k) => { if (/^(title|mode)\b/.test(l)) at = k + 1; });
    L.splice(at, 0, line);
    return L.join('\n');
  }

  const api = { num, vec, deg, scan, callOf, argSpan, getArg, setArg, removeArg, vecLiteral, splitOffset, moveLine, turnLine, scaleLine,
    replaceLine, lineAt, insertAfter, usesOf, removeLine, duplicateLine, shotLines, soundLines, setStatement };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else { root.LE = root.LE || {}; root.LE.source = api; }
})(typeof window !== 'undefined' ? window : globalThis);
