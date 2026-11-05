# Recipes: the common changes, step by step

Every recipe ends with: build, `./build/loop_tests`, and the checks named. A
warning fails the build (`-Werror`).

## Add a world word to Looped Script
1. `engine/script/scene_api.cpp`: `vm.native("name", [](Args& a) { … }, "name(arg, at, material)");`
   - Read args with `a.num/vec/text/flag(pos, "named", fallback)`; pos `-1` = named only.
   - Shapes: build the mesh through `cachedMesh(stageOf(a.vm), keyOf("name", {params}), …)`
     and return `place(a, mesh, kind, size)` so `at/turn/scale/material/name/collide` work.
   - Bad input: `a.vm.fail("plain words", "hint")`.
2. If it returns a thing with fields, extend or add a `ThingClass` (get/set + field list).
3. Bump `stage.version` (or the narrower counter) when the world changes.
4. Test in `tests/scene_api_test.cpp`.
5. `./build/loop words js > web/editor/words.js`; document in `docs/LANGUAGE.md`.
6. If the live renderer must draw something new, also `engine/gpu/`.

## Add an engine setting
1. Field with default in `Config` (`engine/core/settings.h`).
2. Register in `Settings::Settings()` (`settings.cpp`) with `B/I/F/V/T(id, field, [lo, hi,] help)`.
   Id is `area.camelCase`.
3. Read it where it applies via `config().area.field`. If a renderer must rebuild,
   make sure a `watch` covers the prefix (the player watches `render.` and `ultra.`).
4. Editor: entry in `web/editor/settings_schema.js` with `...E()` (bind `set`),
   same id and default.
5. `node tools/check_settings.js` (must pass) and
   `node tools/settings_doc.js > docs/SETTINGS.md`.
6. Add to `docs/TUNING.md` if it trades cost against looks.
7. Turning an `NB(...)` entry into a real one: replace `NB` with the real bind and
   remove the gap from `.claude/notes/engine-gaps.md`.

## Add a named material
`engine/render/materials.cpp` table; it must look right in both renderers (check a
`loop render --quality draft` still and the live player). Update the count and list
in `docs/LANGUAGE.md` and README ("59 named materials"). If it should have a sound,
map it in `audio::surfaceOf`.

## Add a film look
`lookByName` in `engine/render/scene.cpp` and the list in `lookNames()`; docs in
LANGUAGE.md and STORY_GUIDE.md if it carries a mood.

## Add a sound
`engine/audio/audio.cpp`: one-shot in `makeSound`, endless in `makeAmbience`, the
name in `soundNames`/`ambienceNames`; test in `tests/audio_test.cpp` (renders and
checks levels); document in LANGUAGE.md's sound list.

## Add a JS-callable player function
1. `extern "C" EMSCRIPTEN_KEEPALIVE …` in `web/player.cpp`.
2. Add `_name` to `EXPORTED_FUNCTIONS` in `CMakeLists.txt` (else it silently is not there).
3. Call it from `web/loop_ui.js` or `web/editor/engine.js` (guard with `has('name')`).

## Add a test
`TEST(area_what_it_proves) { CHECK(...); CHECK_NEAR(a, b, eps); }` in the matching
`tests/*_test.cpp` (new files are globbed). Run one: `./build/loop_tests area_`.

## Add a project or scene
`projects/<name>/project.json` (`name`, `title`, `start`, `scenes`, `stills`,
optional `settings`, `build`); scene names unique across all projects. Check with
`loop check`, `loop run --seconds 30`, `loop play first.loop --choices …`.

## Verify a visual change
- Cinema: `./build/loop render <scene> --quality draft` and look at the PNG (Read tool).
- Live: `./tools/build_site.sh`, serve `site/`, `xvfb-run -a node tools/web_check.js
  "http://localhost:8000/?scene=<name>&msaa=1" out.png 8`, look at it. Never headless.
- Settings: `xvfb-run -a node tools/settings_browser_check.js http://localhost:8123 out`.
- Editor: `xvfb-run -a node tools/editor_check.js http://localhost:8123 out`.
