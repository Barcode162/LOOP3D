# The bar on LOOP3D, at the same links

**The user, 2026-11-05:** "be able to run the bar on my engine and keep hosting it
from the same links it is and we can forget the old engine and just keep the good
stuff."

## Status: PAUSED (2026-11-05, the user: "pause the full transfer since I need to work on the engine more")
Stages 1–4 are done and pushed; stage 5 is ready but waits for the IONOS secrets
and for this work to reach `main` (GitHub only runs a hand-started workflow that
exists on the default branch). Pick up from "Next time" below.

### Next time
1. **Open issue (cinema only):** a dark oval patch on the hall's floor in
   `loop render` stills of the bar (the live player looks right). Ruled out:
   the floor's mirror-like roughness (fixed by materials.json), lamps trapped in
   their own glowing bulbs (fixed: glowing things no longer block light in the
   path tracer), lamp visibility (dark and lit spots get about the same direct
   light: 6.0 vs 6.8 lux). Next suspects: fog volume bounds, a large unseen
   surface above the floor, emission or normals on the floor mesh. Tools used:
   scratchpad programs that pick the hit under a pixel and sum each lamp's
   light at a point (rebuild them from the plan if needed).
2. High quality lost the software GPU in the headless check; judge it on the phone.
3. Music (needs sample playback), normal maps, then the games (stage 6).
4. Stage 5: the user adds IONOS_SFTP_HOST/USER/PASSWORD to LOOP3D's GitHub
   secrets; the work merges to `main`; run "Bar on the hidden link".

**Goal:** joshuavelasco.com/bar opens the bar drawn and run by LOOP3D. The old
engine (3D's `looped/`, `bar/`) is retired. The good stuff from 3D comes over:
the models, the bar's layout, its lights and their life, the music, the games,
the room server, accounts.

## What the bar is (measured 2026-11-05, 3D main 339ac84)
- **167 models** (GLB, 30 MB, 446k unique triangles), all geometry ours (Blender
  scripts in 3D `tools/assets/`). Textures: **387 JPEGs, all baseline, 4:2:0**,
  64–1024 px. Materials: glTF metal/roughness with base colour, metal-rough and
  normal maps; 40 models use `KHR_materials_emissive_strength`. Attributes:
  POSITION, NORMAL, TEXCOORD_0, TANGENT (float), indices u16/u32.
- **The place** (`assets/world/places/bar.looped`, 607 lines): 3 rooms, 2
  doorways, 276 bottles, 70 wall props, 36 chess pieces, ~85 lights (bulbs, neon,
  spots, grooves, candles, embers), stools, tables, glasses, the piano, rigs…
  Kinds in `assets/world/kinds/bar.json`; the light rig `assets/world/rigs/bar.looped`
  (10 channels: breathe, buzz, thump to the music, lights down).
- **Hosting:** 3D's `deploy.yml` builds and uploads to IONOS `home/bar` over SFTP
  (`lftp`), with secrets `IONOS_SFTP_HOST/USER/PASSWORD` that live in the 3D repo.
  The PHP next to the game (`account.php`, `report.php`, `gate.php`, `.htaccess`)
  and the Cloudflare room are deployed by the same workflow. The bar's page
  already redirects `?engine=next` to `labs/next/`: the same trick gives a hidden
  link for LOOP3D.

## Stages (each one something to look at on the phone)
| # | Stage | What you'll see | Status |
|---|---|---|---|
| 1 | **Models in LOOP3D**: our own JPEG decoder and GLB loader; a `model "x.glb"` word | a cinema still of the bar's stool, table and counter, textured | **done 2026-11-05**: all 167 models load (446,636 triangles, 387 pictures, colours within 0.17/255 of a standard decoder); still with stool, table, sofa, jukebox, bottles, glass, candle, lamp; 3 tests |
| 2 | **Textures in the live renderer** (base colour, roughness/metal, normal maps) on WebGPU | the models in the player, same as the still | **done 2026-11-05**: one GPU texture stack (`render.textureSize`, 256 px), 186 shared pictures, colour + roughness/metal + alpha cut; the page fetches `.glb` files into the engine's own folder. Normal maps not yet (both renderers) |
| 3 | **The bar as a LOOP3D project**: a converter from bar.looped + kinds to `projects/bar3d/` (`.loop` scenes, models copied), lamps in lumens/K, rooms, blockers, spawn | walk the whole bar in the player | **done 2026-11-05**: `tools/import_bar.py <3D repo>` → `projects/bar3d/` (507 things, 103 lamps incl. panels and tubes, 293 invisible blockers from walls, furniture and raised floors, real material values from 3D's albedo.json as `models/materials.json`). Draws live on Phone quality in SwiftShader; High lost the software GPU (needs the phone) |
| 4 | **Its life**: the light rig as Looped Script (breathe, buzz, music thump, lights down), the fire, the jukebox music (CC-BY), ambience | the bar breathing and buzzing | **mostly done 2026-11-05**: every lamp breathes/buzzes/waves per the rig, the switch by the door dims the bar to 25%, the fire crackles. Not yet: music (needs sample playback) and the strings dancing to it |
| 5 | **Hidden link**: LOOP3D's site uploaded to `home/bar/loop3d/` (joshuavelasco.com/bar/loop3d/?scene=bar) | the LOOP3D bar at a link only you know | **ready, needs the user**: workflow `.github/workflows/bar-hidden.yml` (manual, never deletes); the 3 IONOS secrets must be added to LOOP3D's GitHub settings |
| 6 | **The games, one at a time**: seats and taps, the menus, jukebox, piano, darts, chess, 4K Mania, racing; then the room server (multiplayer, voice, accounts) | each game working on the new engine | |
| 7 | **The switch**: `joshuavelasco.com/bar` serves LOOP3D; the old engine retired | the bar on LOOP3D for everyone | when the user says |

## Decisions
- **Hosting route (stage 5).** Recommend: LOOP3D gets its own upload workflow,
  which needs the three IONOS secrets added to LOOP3D's GitHub settings (only the
  user can; secrets can't be copied between repos). Uploads must **never delete**
  what's beside the game (the PHP, `labs/`, the old bar until the switch):
  `lftp mirror -R` without `--delete`, into its own folder first.
  Other route: 3D's workflow checks out LOOP3D and builds it (no new secrets, but
  ties the new engine to the old repo).
- **Multiplayer:** keep the Cloudflare room and its protocol (3D `cloud/`,
  `docs/handbook/protocol.md`); LOOP3D's player learns to speak it in stage 6.
- **WebGL2:** the user wants every browser; LOOP3D is WebGPU-only today. Not
  needed to reach the switch on the user's phone (iOS 26 has WebGPU); a later stage.

## Risks
- Phone speed: 446k unique triangles, many copies (276 bottles). LOOP3D instances
  them; no LOD yet (3D's clusters can come over if needed).
- Texture memory: 387 JPEGs; decode once, downsize big ones for Phone quality.
- Light: ~85 lamps. LOOP3D's live renderer lights up to 256 per pixel and shadows
  the brightest 2 on High; clustered lights are on the gap list.
- The upload must not touch the live bar until the switch.
