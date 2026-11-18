# Island — full sprite list (361 pictures)

Every sprite the full game needs, one line per set. **1 picture = 1 sprite.**
A crop's 4 growth stages count as 4 pictures. A recolour or upgrade tier counts
as its own picture. Counts in each heading are pictures.

Rotation rules are at the bottom. Left/right may be mirrors — draw right, flip
for left — so a mirrored direction is not a separate picture.

## A — Characters & animation (50)

Player (24):
- Walk down, frames 1–4 (4)
- Walk up, frames 1–4 (4)
- Walk left, frames 1–4 (4)
- Walk right, frames 1–4 (4)
- Tool poses: hoe, water, fish, chop, mine (5)
- Idle, sleep (2)
- Portrait (1)

NPCs (16) — each one sprite (front) + one portrait:
- Sailor Bram, Shopkeeper Mia, Fisher Finn, Blacksmith Ash, Baker Rose,
  Kid Pip, Elder June, Merchant Sol (8 × 2 = 16)

Animals (10) — side view each:
- Dog, cat, chicken, cow, pig, sheep, goat, duck, crab, seagull

## B — Crops (72)

12 crops × 6 pictures each (seed, sprout, growing, mature, seed packet,
produce icon):
- Wheat, tomato, pumpkin, carrot, corn, strawberry, potato, berry, cabbage,
  onion, melon, chilli (12 × 6 = 72)

## C — Trees & nature (46)

- Trees, 6 kinds × 4 seasons: palm, oak, pine, apple, birch, willow (24)
- Berry bushes: blueberry, raspberry, blackberry (3)
- Flowers: daisy, tulip, rose, sunflower, lavender, poppy (6)
- Grass tufts (4)
- Rocks (3)
- Shells: pink, spiral, white, pearl (4)
- Seaweed (2)

## D — Fishing & sea (23)

- Fish (15): tuna, salmon, tropical, herring, anchovy, bass, carp, catfish,
  eel, pufferfish, sardine, mackerel, trout, koi, anglerfish
- Sea creatures (5): clam, starfish, crab, shrimp, octopus
- Bait / tackle (3): worm bait, lure, bobber

## E — Tools & gear (24)

8 tools × 3 tiers (basic, copper, iron):
- Hoe, watering can, axe, pickaxe, fishing rod, scythe, bucket, hammer

## F — Items, materials & food (28)

- Materials (8): wood, stone, fiber, clay, coal, hardwood, sap, bait
- Forage (8): mushroom, wild berry, driftwood, seaweed, shell, herb, flower,
  feather
- Dishes (12): salad, fish stew, grilled fish, pumpkin soup, berry pie,
  omelette, bread, jam, juice, roast corn, sea soup, cake

## G — Buildings (15)

- Farmhouse, farmhouse upgrade, barn, coop, silo, stable, well, lighthouse,
  dock, fish shop, market stall, bridge, greenhouse, windmill, shed

## H — Terrain tiles (38)

- Grass (6), dirt (4), tilled dry (2), tilled watered (2), sand (4),
  water (4), stone path (4), wood floor (4), cliff (4), beach edge (4)

## I — Decor & props (25)

- Fence: wood, stone, hedge (3)
- Scarecrow (2), torch (2), lamp (2), sign (3), flower pot (3), bench (2)
- Campfire (1), chest (2), barrel (2), hay bale (2), mailbox (1)

## J — UI & screens (30)

- Inventory slots (4), hearts (5), stamina (2), clock (1), season icons (4),
  weather icons (3), buttons (6), dialogue boxes (2), menus (2),
  title logo (1)

## K — Atmosphere & effects (10)

- Day sky, sunset, night, rain, clouds (3), sparkles, splash, bubbles

## Rotation / views — what needs shots from all angles

| Thing | Views needed | Note |
|---|---|---|
| Player | 4 directions (down, up, left, right) | Walk frames in every direction; tool poses face down mostly |
| NPCs | Front + side + back | Front for dialogue, side/back for walking |
| Animals | Side (1) | Flip the drawing for the other direction |
| Fish | Side (1) | One side view each |
| Crops | Top-down (1) | Same view at every growth stage |
| Trees | Front (1) | Symmetric, no rotation needed |
| Buildings | Front (1) | Front only; a back view is never seen |
| Tools, items, food | 1 | Single view each |
| Terrain tiles | Top-down (1) | Same view, tiled |
| UI, atmosphere | 1 | Single view each |

So only **characters** truly need a bunch of shots from all angles: the player
(4 directions) and NPCs (3 angles). Everything else is 1 view, mirrored or
tiled where it fits.

## Status

- Drawn so far: 385 sprites (the 32 sheets in `INDEX.md`). Demo art is
  complete: 103 of 103. Full game: all 361 planned pictures are drawn, plus
  24 extra rotation / duplicate shots.
- Demo needs 103; full game needs 361.
- See `PLAN.md` for the demo-vs-full contrast, `INDEX.md` for the list of
  what is actually drawn so far, and `USAGE.md` for where each asset is used.
