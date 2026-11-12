code:     T-K5
title:    The curve under your thumb
hand:     lead
size:     M
wave:     t1
branch:   claude/3d-editor-animations-rigging-sqjd71
status:   done
touch:    web/editor/curves.js, web/editor/studio.js, web/editor/editor.css, web/editor/icons.js, web/editor/editor.html, tools/editor_check.js, engine/anim/clip.h, engine/anim/clip.cpp, tests/clip_test.cpp
depends:  T-K4
pictures: no
expected: 7
started:  2026-11-11T19:41Z
updated:  2026-11-12T03:04Z
finished: 2026-11-12T03:04Z
note:     2026-11-12 T-K5, the curve under your thumb (goal 2: the slide starts gently and lands softly): a picked key shows how it arrives as four big buttons (held, straight, smooth, free) and seven eases by name, and its value's curve drawn large from the engine's own numbers (loop_clip_curve), so what is drawn is what plays and what film renders; a free key has round handles sized for a thumb (together or each its own), starting exactly as the smooth curve it was (each key now reports its slopes); one finger slides the white line along the curve, two fingers slide and pinch the view. Checked in a browser on phone and computer sizes (7 new checks each); the check now takes a third word, phone or computer. 253 tests.
