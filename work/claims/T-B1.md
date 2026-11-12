code:     T-B1
title:    The standard skeleton, ring 1: 67 bones, names, limits
hand:     lead
size:     M
wave:     t1
branch:   claude/3d-editor-animations-rigging-sqjd71
status:   done
touch:    library/skeletons/human.loop, engine/anim/skeleton.h, engine/anim/skeleton.cpp, engine/anim/anim.h, engine/anim/anim.cpp, engine/project/library.h, engine/project/library.cpp, tests/skeleton_test.cpp
pictures: no
expected: 7
started:  2026-11-11T19:41Z
updated:  2026-11-12T07:51Z
finished: 2026-11-12T07:51Z
note:     2026-11-12 T-B1, the standard skeleton, ring 1: the words skeleton, bone and bones; library/skeletons/human.loop, 66 bones (the plan said 67; its own list adds to 66), 1.75 m at rest, every left bone with its right twin, each joint's limits as bend, side, twist from the published ranges of motion; a pose pushed past a limit is held at the nearest allowed turn; a wrong skeleton is refused by name and line. New scene projects/samples/skeleton.loop. 9 new tests, 262 in all. No picture tests run: the renderer did not change.
