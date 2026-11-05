# Tools and tests

## `loop` (tools/loop.cpp)
`check` (compile only) · `run [--seconds N]` (no picture, prints `say`, reports
run-time problems) · `play first.loop [--choices 0,1]` (whole story through every
`go_to`, autoplay presses Use, prints lines with times) · `render [--quality]
[--width] [--samples] [--look]` (stills/films the script asks for, else one still)
· `demo studio|valley` · `settings list` (JSON) · `words json|js`.

## Node / Python tools
| Tool | Purpose |
|---|---|
| `check_settings.js` | editor schema ↔ engine registry agree (names, defaults) |
| `settings_doc.js` | writes `docs/SETTINGS.md` |
| `web_check.js` | player screenshot + page problems (xvfb, SwiftShader) |
| `settings_browser_check.js` | every live setting set, read back, drawn with |
| `editor_check.js` | editor end to end in a browser |
| `build_projects.py` | projects → `site/scenes/` |
| `import_bar.py <3D repo>` | the original bar → `projects/bar3d/` (scene, models, credits, real material values); never edits 3D |
| `perf_add.py` | benchmark JSON → `perf/raw/`, rebuild `runs.json`, `segments.json` |

## Tests
Own runner (`tests/test.h`): `TEST(name)`, `CHECK`, `CHECK_NEAR`; `loop_tests
<substring>` filters. 70 tests in 10 files; full build ~20 s, tests pass at
`a56c14c`. CI: only the manual Pages workflow runs them (no CI on push).

## Gaps
No CI on push/PR, no JS unit tests for the editor, no image-diff regression tests
for renders, no fuzzing of the parser, no sanitizer builds.
