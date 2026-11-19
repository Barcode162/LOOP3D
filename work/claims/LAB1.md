code:     LAB1
title:    Light lab: measure every way of lighting against the film picture
hand:     builder
size:     S
branch:   claude/quirky-einstein-em398p
status:   waiting
touch:    docs/LIGHT_LAB.md perf/lab tools/light_lab.js engine/render/surface_light.h engine/render/surface_light.cpp tests/surface_light_test.cpp engine/render/pathtracer.h engine/render/pathtracer.cpp engine/gpu/renderer.h engine/gpu/renderer_internal.h engine/gpu/renderer_light.cpp engine/gpu/renderer_draw.cpp engine/gpu/shaders/common.wgsl.h engine/gpu/shaders/scene.wgsl.h engine/core/settings.h engine/core/settings.cpp web/editor/settings_schema.js docs/SETTINGS.md web/player.cpp
pictures: no
expected: 6
started:  2026-11-19T01:45Z
updated:  2026-11-19T07:20Z
note:     round 3 done on claude/quirky-einstein-em398p: the light bake (render.bake, off by default) takes the Loft from 16.1 to 10.9 from film, 1.8x to 1.2x bright; next: ship it baked, time it on the phone
