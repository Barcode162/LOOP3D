code:     T-K1
title:    The clip: channels, keys, curves, as text
hand:     lead
size:     M
wave:     t1
branch:   claude/3d-editor-animations-rigging-sqjd71
status:   done
touch:    engine/anim/clip.h, engine/anim/clip.cpp, tests/clip_test.cpp, tests/anim_test.cpp
pictures: no
expected: 7
started:  2026-11-11T19:41Z
updated:  2026-11-11T19:46Z
finished: 2026-11-11T19:46Z
note:     2026-11-11 T-K1, the clip: one format for everything that moves by keys (engine/anim/clip.*): channels of keys; a key arrives smooth (no overshoot, gentle at a clip's ends), straight, held, by a named ease, or with its own slopes; once, loop (the join has no jolt) and back and forth; a bone's turn as bend, side, twist (there and back to 0.01 degree over swing to 175 and twist to 179 degrees); a clip writes itself as script. The old anim::Clip is gone (nothing used it). 244 tests.
