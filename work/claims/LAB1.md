code:     LAB1
title:    Light lab: measure every way of lighting against the film picture
hand:     builder
size:     S
branch:   claude/quirky-einstein-em398p
status:   working
touch:    docs/LIGHT_LAB.md perf/lab tools/light_lab.js engine/render/surface_light.h engine/render/surface_light.cpp tests/surface_light_test.cpp engine/render/pathtracer.h engine/render/pathtracer.cpp engine/gpu/renderer.h engine/gpu/renderer_internal.h engine/gpu/renderer_light.cpp engine/gpu/renderer_draw.cpp engine/gpu/shaders/common.wgsl.h engine/gpu/shaders/scene.wgsl.h engine/core/settings.h engine/core/settings.cpp web/editor/settings_schema.js docs/SETTINGS.md web/player.cpp
pictures: no
expected: 6
started:  2026-11-19T01:45Z
updated:  2026-11-19T07:14Z
note:     round 3 on claude/quirky-einstein-em398p: the light bake (render.bake, off by default), additive; overlaps the silent N8 claim (8 h, no branch) in renderer_light, scene.wgsl, settings
