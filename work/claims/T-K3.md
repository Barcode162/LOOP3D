code:     T-K3
title:    The engine's hands for keys
hand:     lead
size:     M
wave:     t1
branch:   claude/3d-editor-animations-rigging-sqjd71
status:   done
touch:    web/player.cpp, web/editor/engine.js, CMakeLists.txt, engine/anim/clip.h, engine/anim/clip.cpp, tests/clip_test.cpp
depends:  T-K2
pictures: no
expected: 7
started:  2026-11-11T19:41Z
updated:  2026-11-12T02:22Z
finished: 2026-11-12T02:22Z
note:     2026-11-12 T-K3, the engine's hands for keys: loop_clips (every clip, what it is on, its moment, its keys), loop_clip_set (keys, settings, moment, playing at once, no rebuild; -1 makes a new clip), loop_clip_moment, loop_clip_curve (a value as the engine fills it in, for drawing); a clip's JSON form in engine/anim/clip.* (tested natively); checked in a real browser: scrub to half way, a changed key shows at once, a wrong change refused; the editor's browser check passes on phone and computer. 253 tests.
