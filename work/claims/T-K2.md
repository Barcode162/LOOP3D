code:     T-K2
title:    Keys in a scene: the words, and the stage plays them
hand:     lead
size:     M
wave:     t1
branch:   claude/3d-editor-animations-rigging-sqjd71
status:   done
touch:    engine/script/words_anim.cpp, engine/script/scene_api.h, engine/script/scene_api.cpp, engine/script/words_internal.h, engine/script/parser.cpp, tests/words_test.cpp, docs/LANGUAGE.md, projects/samples/keys.loop, projects/samples/project.json
depends:  T-K1
pictures: no
expected: 7
started:  2026-11-11T19:41Z
updated:  2026-11-11T19:54Z
finished: 2026-11-11T19:54Z
note:     2026-11-11 T-K2, keys in a scene: the words clip and key (engine/script/words_anim.cpp; the parser lets clip take indented lines as group does); the stage plays every clip on the one clock, first in its step (once, loop, back and forth; forwards, backwards, held); a thing's at, turn, tilt, roll, scale, visible, a lamp's at, lumens, color, kelvin, a group's at, turn, visible; a clip is a value (play, reverse, stop, .time); seekClip scrubs without the world's time moving; keys inside a group move and turn with it; a keyed thing is a wall where it comes to rest; a clip written out reads back the same. Not done, cut as its own order: a thing's colour by keys (it needs a cheap path for material numbers in the live renderer). New sample scene Keys. 252 tests.
