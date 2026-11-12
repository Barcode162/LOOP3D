code:     T-K4
title:    The Move bench: a timeline with keys
hand:     lead
size:     M
wave:     t1
branch:   claude/3d-editor-animations-rigging-sqjd71
status:   done
touch:    web/editor/words.js, web/editor/studio.js, web/player.cpp, web/editor/engine.js, CMakeLists.txt, engine/anim/clip.h, engine/anim/clip.cpp, engine/script/scene_api.h, engine/script/words_anim.cpp, tests/words_test.cpp, tools/build_site.sh, web/editor/source.js, web/editor/spaces.js, web/editor/editor.css, web/editor/icons.js, web/editor/editor.html, web/editor/editor.js, web/editor/view.js, tests/editor_source_test.js, tools/editor_check.js
depends:  T-K3
pictures: no
expected: 7
started:  2026-11-11T19:41Z
updated:  2026-11-12T02:39Z
finished: 2026-11-12T02:39Z
note:     2026-11-12 T-K4, the Move bench (goal 1: a box slides between two keys): Open on a picked thing shows the Studio (Shape and Bones say what is coming; Move works); a strip of time with a row for each keyed value and a diamond for each key; slide the white line, move the thing, and that is a key (the thing gets a name, its clip and key lines go in after it, written by the engine itself); pick, drag and remove keys; Play, Start, Key here, More time; stops, starts again, goes back; undo and redo without a rebuild; Done builds the world again. Checked in a browser on phone and computer sizes (tools/editor_check.js: 11 new checks each). For now a thing inside a group or made by a repeat is not opened. 253 tests, 45 editor source checks.
