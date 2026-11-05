# Scene API and Stage (engine/script/scene_api.*)

The bridge from words to the world. `installSceneApi(vm, stage)` registers:
`setting settings get_setting title mode material materials box sphere cylinder
cone capsule ring ground rock tree terrain height_at forest light panel_light sun
sky fog camera look shot orbit solid push gravity person sound room line ask
remember recall forget_story player near move_player go_to clear_lines render film`.

## Stage (scene_api.h)
- `scene` (render::Scene), `film` (shots), `renders` (RenderRequest list),
  `terrains` (a **deque**: physics holds pointers), `placed` (one per script-visible
  instance: position, scale, rotation yaw/pitch/roll or quat, kind b/s/c/m, body id,
  collide), `people` (skeleton + animator + body parts), `sounds`, `world`
  (physics), `mixer` (audio), `meshCache` (shape key → mesh index).
- Change counters: `version` (anything; full resync), `transformsVersion`,
  `lightsVersion`. Bump the narrowest one that is true.
- Story/UI: `ui` messages (line/ask/clear), `nextScene`, `playerPos`, `autoplay`,
  `teleport*`, `story`.
- `update(dt)`: physics step, people animators (footsteps → mixer with the floor's
  surface), sounds follow; `setListener(camera)`.

## Thing classes (~line 157)
material, shape (`position at turn rotation scale material visible name`),
light (`position at lumens brightness color kelvin radius aim on`),
person (`position at walk_to look_at sitting speed facing walking visible lift`),
sound (`volume position at playing`), terrain (`size lowest highest`, read-only).

## Behaviour worth knowing
- Simple shapes collide with the walker unless `collide: no`; `solid()` gives them a
  physics body and automatic impact sounds by material.
- People walk **straight** to `walk_to` (no steering, no navmesh).
- `story.memory` (`remember/recall`) survives `go_to`; plain globals do not.
- Changing a shape's `.material` or making shapes while playing bumps `version` →
  full world re-read on the GPU. Cheap: position/turn/scale, light fields.
