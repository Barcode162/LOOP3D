# Looped Script (engine/script/)

**Files:** `lexer.*` (232) → `parser.*` (796) → `ast.h` → `compiler.*` (473) →
`bytecode.h` → `vm.*` (995) + `value.h`; `stdlib.cpp` (457); `scene_api.*` (1118).
User doc: `docs/LANGUAGE.md`. Quick reference: [../language-quickref.md](../language-quickref.md).

## How it works
- **Lexer:** indentation (2 spaces) → `kIndent`/`kDedent`; numbers carry units and are
  converted to base units at read time (`unitScale`); `Token.unit` keeps what was written.
  Multi-word setting names are joined with `_` (`look at:` = `look_at:`).
- **Parser:** recursive descent into `Stmt`/`Expr`; resolves locals to `Decl`s and marks
  `captured` ones (they live in `Cell`s). Command calls without brackets (`say "x"`,
  `box size: …`). Optional `:` after a header allows a one-line body.
- **Compiler:** one `Proto` per function; ops are 1 byte + 16-bit operands;
  `lines[]` maps every byte to a source line (errors and the runaway-loop stop use it).
  Named args: `OP_CALL argc, named, k(names)`. `every/after/when/task` compile to
  `OP_SCHEDULE kind` with a closure.
- **VM:** stack machine with fibers. `update(dt)` advances `time_`, fires timers
  and handlers, resumes waiting fibers. Step `budget` 2,000,000 per fiber per update.
  Mark-and-sweep GC over the `heap_` list, roots = globals, fibers, timers,
  handlers, `pinned_`.
- **Values:** numbers are double; vec is 3 floats inline (colours are vecs too);
  `nothing`, `unset` (a parameter not given, for defaults/`OP_REQUIRE`).
- **Errors:** `Problem{file,line,column,what,hint}`; `closest()` (edit distance)
  feeds "did you mean"; built-ins `vm.fail(what, hint)` (throws, caught in the VM;
  the web build compiles with `-fexceptions` for this).

## Extending
- Built-in: `vm.native(name, fn, help)`; `help` is shown in errors and becomes the
  editor's suggestion text (`loop words js`).
- Methods: `vm.method("list"|"text"|"vec"|"table"|<ThingClass name>, name, fn)`.
- Engine objects: `Thing` + `ThingClass{name, get, set, fields}`.

## Tests
`tests/script_test.cpp` (12: units, interpolation, closures, named args, command
calls, loops, lists/tables/vectors, wait/every/after/when, …),
`tests/scene_api_test.cpp` (3), `tests/settings_test.cpp` (script words for settings).

## Gaps
`script.strict` (warn about unused names) is NB. No debugger/breakpoints, no
module system beyond textual `use`, no static types; check the gap list.
