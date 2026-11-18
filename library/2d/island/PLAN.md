# Island — plan: full working world vs demo

A survival island farming and fishing game, Stardew Valley style, in cozy pixel art.

Counting rule: **1 picture = 1 sprite.** A crop's 4 growth stages count as 4 pictures.
A recolour or an upgrade tier counts as its own picture. One sheet holds about
13–15 pictures. Counts below are estimates to plan against, not promises.

## What the full working world needs — 361 pictures, ~26 sheets

| # | Category | Pictures | What is in it |
|---|----------|----------|---------------|
| A | Characters & animation | 50 | Player: walk 4 dirs × 4 frames (16), tool poses (5), idle/sleep (2), portrait (1). 8 NPCs × sprite + portrait (16). 10 animals. |
| B | Crops | 72 | 12 crops × 4 growth stages (48) + 12 seed packets + 12 produce icons. |
| C | Trees & nature | 46 | 6 trees × 4 seasons (24), berry bushes (3), flowers (6), grass (4), rocks (3), shells (4), seaweed (2). |
| D | Fishing & sea | 23 | 15 fish, 5 sea creatures, bait/tackle (3). |
| E | Tools & gear | 24 | 8 tools × 3 upgrade tiers. |
| F | Items, materials & food | 28 | Materials (8), forage (8), cooked dishes (12). |
| G | Buildings | 15 | Farmhouse + upgrade, barn, coop, silo, stable, well, lighthouse, dock, fish shop, market stall, bridge, greenhouse, windmill, shed. |
| H | Terrain tiles | 38 | Grass (6), dirt (4), tilled dry (2), tilled watered (2), sand (4), water (4), stone path (4), wood floor (4), cliff (4), beach edge (4). |
| I | Decor & props | 25 | Fence (3), scarecrow (2), torch (2), lamp (2), sign (3), flower pot (3), bench (2), campfire (1), chest (2), barrel (2), hay (2), mailbox (1). |
| J | UI & screens | 30 | Inventory slots (4), hearts (5), stamina (2), clock (1), season icons (4), weather icons (3), buttons (6), dialogue boxes (2), menus (2), title logo (1). |
| K | Atmosphere & effects | 10 | Day sky, sunset, night, rain, clouds (3), sparkles, splash, bubbles. |
| | **Total** | **361** | **~26 sheets** |

## What else a full world needs besides pictures

Systems (code, not pictures): farming (till / plant / water / grow / harvest),
fishing minigame, day/night clock + stamina, inventory + chest storage, shop and
selling, NPC dialogue + friendship, animal care, 4 seasons + weather, crafting +
cooking, quests + tutorial, island map + layout, save/load. Plus audio: music and
sound effects (not counted here).

## What the demo is — 103 pictures, ~8 sheets

One small island, one season (summer), one playable day, start to finish:

Wake up → till plots → plant 4 crops → water them → fish 3 catches off the dock →
feed the chicken → sell at the market stall → sleep, crops grow overnight.

| # | Category | Demo | What is in it |
|---|----------|------|---------------|
| A | Characters & animation | 20 | Player: walk 4 dirs × 2 frames (8), idle (1), tool poses (2), portrait (1). NPCs: sailor + shopkeeper with portraits (4), fisher (1). Animals: dog, chicken, crab (3). |
| B | Crops | 24 | 4 crops (wheat, tomato, pumpkin, carrot) × 4 stages (16) + 4 seed packets + 4 produce icons. |
| C | Trees & nature | 10 | Palm, oak, berry bush, flowers (2), grass (2), rocks, shells (2). |
| D | Fishing & sea | 7 | 5 fish + clam + starfish. |
| E | Tools & gear | 5 | Hoe, watering can, axe, fishing rod, bucket. |
| F | Items & materials | 8 | Wood, stone, fiber, bait, mushroom, wild berry, driftwood, seaweed. |
| G | Buildings | 5 | Farmhouse, coop, dock, well, bridge. |
| H | Terrain tiles | 9 | Grass (2), dirt, tilled dry, tilled watered, sand, water (2), path. |
| I | Decor & props | 5 | Fence, scarecrow, campfire, chest, torch. |
| J | UI & screens | 7 | Inventory slot, heart, stamina, clock, coin, button, dialogue box. |
| K | Atmosphere | 3 | Day sky, night, rain. |
| | **Total** | **103** | **~8 sheets** |

Demo systems only: farming, fishing, day/night, inventory, shop selling, 2 NPC
dialogues, feeding 1 animal, save. Not in the demo: seasons, weather systems,
crafting, cooking, tool upgrades, 6 extra NPCs, 8 extra crops, 10 extra fish.

## The contrast — demo vs full

| | Full world | Demo | Demo as a share of full |
|---|-----------|------|------------------------|
| Pictures | 361 | 103 | **28.5%** |
| Sheets | ~26 | ~8 | ~31% |
| Crops | 12 | 4 | 33% |
| Fish | 15 | 5 | 33% |
| NPCs | 8 | 3 | 38% |
| Animals | 10 | 3 | 30% |
| Seasons | 4 | 1 | 25% |
| Systems | 14 | 7 | 50% |

The demo is about a quarter of the art and half the systems. It plays the whole
core loop — farm, fish, sell, sleep — with nothing that is only half a feature.

## Where we already are

The 32 sheets in `INDEX.md` hold 385 sprites. Mapped against the two targets:

| | Full (361) | Demo (103) |
|---|-----------|-----------|
| Already have | 385 (all 361 planned pictures, + 24 extra rotation / duplicate shots) | 103 (100%) |
| Still missing | 0 — full-game art is complete | 0 — demo art is complete |

The 4 extra mature crop stages already drawn (corn, strawberry, potato, berry)
count toward the full game, not the demo — the demo only grows 4 crops. The
NPC side/back views and the goat/duck/dog/cat/pig side views are full-game
rotation shots, not demo art either.

## Demo art — complete

Sheets 7 and 8 from the list below are done (stuff/UI/atmosphere, ground &
nature). The demo art is complete: 103 pictures, 10 sheets, and the game is
drawable end to end. Everything after it is full-game growth: 233 more
pictures (~17 sheets) for seasons, extra NPCs, crops, fish, upgrades, cooking
and crafting.

## Full-game art — complete (2026-11-18)

The remaining 233 pictures were generated as 21 more sheets (12–32 in
`INDEX.md`): player-extra, npc-new, crops-2/3/4, trees-seasons 1–2, nature-2,
fish-2, tools-2/3, food-1/2, buildings-2, terrain-2/3, decor-2,
decor-atmosphere, ui-2/3, fixes. All 385 sprites are sliced to individual
PNGs (`~/workspace/tidehollow-demo/sprites/`, one folder per sheet) and
verified by count and by montage — two fixes along the way: the sleeping pose
was missing from player-extra (added on fixes), and ui-3's boxed labels were
being sliced as sprites (slicer now drops wide flat groups). `USAGE.md` maps
every asset to the system and place that uses it.
