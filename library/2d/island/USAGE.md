# Island — where every asset is used

The logic that ties the art to the game: for every sprite, which system uses
it, where on the island it lives, and what it does. If a sprite is not in
this file, the game does not need it. See `INDEX.md` for the full list of
what is drawn and `SPRITES.md` for the 361-picture plan.

## The island map

The demo is one small screen (the farm + dock corner). The full game is a
bigger island with a scrolling camera. Zones:

| Zone | Where | What lives there |
|------|-------|------------------|
| Farm | South-west | Farmhouse (+ upgrade), plots, barn, coop, stable, silo, greenhouse, shed, windmill, scarecrows, fences, mailbox, chest |
| Village | Centre | Market stall, fish shop, bakery (Rose), blacksmith (Ash), well, benches, signs, lamps, flower pots |
| Dock & beach | South-east | Dock, boat, sand, beach edge, shells, seaweed, crab, seagull |
| Forest | North | Trees × 4 seasons, berry bushes, flowers, grass, rocks, forage, animals |
| Lighthouse point | North-east | Lighthouse, cliff tiles, the Drowned Bell (story) |
| Reef | East, low tide only | Shells, starfish, clam, shrimp, octopus |

## The rules

1. **One asset, one primary use.** A sprite belongs to one system; it may
   appear in other places (a fish is caught at the dock, sold at the stall,
   cooked in the kitchen) but its home is one row of this file.
2. **Tiles are placed by zone, never by hand.** Grass/dirt/sand/water/path/
   wood-floor/cliff/beach-edge tiles fill their zone; only the variants
   (flowers, rocks, shells) are placed one by one.
3. **A crop is six pictures, always together.** Seed, sprout, growing,
   mature, seed packet, produce — the packet lives in the shop, the first
   four live on the plot, the produce lives in the inventory.
4. **A tool is three pictures, always together.** Basic, copper, iron — the
   player carries one tier at a time; the other two live in the shed / shop.
5. **An NPC is front + side + back + portrait.** Front for talking, side and
   back for walking, portrait for the dialogue box. No NPC walks without all
   four.
6. **UI is never drawn on the map.** Slots, hearts, stamina, clock, buttons,
   menus and the title logo live only on screens, never as world objects.
7. **Atmosphere is a layer, not an object.** Skies, clouds, rain, fog,
   sparkles, splash and bubbles overlay the world; they are never placed
   like furniture.

## A — Characters & animation

| Asset | System | Where | Role |
|---|---|---|---|
| Player walk 4 dirs × 4 frames (16) | Movement | Everywhere | Walk cycle, direction = input |
| Player tool poses: hoe, water, fish, chop, mine (5) | Farming / fishing / gathering | Farm, dock, forest | Action pose while the action runs |
| Player idle (1) | Movement | Everywhere | Standing still |
| Player sleep (1) | Day/night | Farmhouse bed | Sleeping pose on the sleep screen |
| Player portrait (1) | Dialogue | Dialogue box | The player's face when they speak |
| NPCs × 8: front, side, back, portrait | NPCs / friendship | Village, dock, farm | Walk the island, talk, give quests |
| Bram (sailor) | NPCs / fishing | Dock | Teaches fishing, sells bait |
| Mia (shopkeeper) | Shop | Market stall | Buys produce and fish, sells seeds |
| Finn (fisher) | NPCs / fishing | Dock end | Fishing tips, the crab-tax joke |
| Ash (blacksmith) | Crafting / upgrades | Blacksmith | Upgrades tools, forges the Counter-Bell |
| Rose (baker) | Cooking | Bakery | Buys wheat, sells bread, bakes dishes |
| Pip (kid) | Story / tutorial | Dock, farm | Tutorial guide, talks to the scarecrow |
| June (elder) | Story | Farmhouse, well | Holds the Night of Empty Tables |
| Sol (merchant) | Shop | Village, dock | Travelling trader, rare seeds and fish |
| Animals × 10: dog, cat, chicken, cow, pig, sheep, goat, duck, crab, seagull | Animals | Farm, beach, forest | Dog follows, chicken lays eggs, crab steals the first catch, seagull circles the dock |

## B — Crops (12 × 6)

| Crop | System | Where | Role |
|---|---|---|---|
| Wheat, tomato, pumpkin, carrot | Farming (demo + full) | Farm plots | The demo's four crops; wheat → bread at the bakery |
| Corn, strawberry, potato, berry | Farming (full) | Farm plots | Full-game crops; berry also grows wild in the forest |
| Cabbage, onion, melon, chilli | Farming (full) | Farm plots, greenhouse | Greenhouse crops in winter; chilli is Sol's rare seed |
| Seed / sprout / growing / mature | Farming | On the plot | Growth stages, one per sleep if watered |
| Seed packet | Shop | Market stall, inventory | What Mia and Sol sell |
| Produce | Inventory / shop / cooking | Inventory, stall, kitchen | Sell it, eat it, cook it |

## C — Trees & nature

| Asset | System | Where | Role |
|---|---|---|---|
| Palm, oak, pine, apple, birch, willow × 4 seasons | Seasons / gathering | Forest, farm edges, beach | Change look every season; chop for wood, shake for fruit |
| Berry bushes × 3 | Foraging | Forest | Pick berries (blueberry, raspberry, blackberry) |
| Flowers × 6 | Foraging / gifts | Forest, farm, village pots | Pick, gift to NPCs, cook with |
| Grass tufts × 4 | Terrain | Everywhere | Ground decoration, cut with the scythe for fiber |
| Rocks × 3 | Gathering | Forest, beach, cliff | Mine with the pickaxe for stone |
| Shells × 4 | Foraging | Beach, reef | Pick up, sell, craft with |
| Seaweed × 2 | Foraging | Beach, reef | Pick up, cook with |

## D — Fishing & sea

| Asset | System | Where | Role |
|---|---|---|---|
| Fish × 15 | Fishing | Dock, sea, reef | Caught in the timing minigame; rarity by season and spot |
| Tuna, salmon, tropical, herring, anchovy | Fishing (demo + full) | Dock | The demo's five fish |
| Bass, carp, catfish, eel, pufferfish, sardine, mackerel, trout, koi, anglerfish | Fishing (full) | Dock, sea, reef | Full-game fish; koi at the reef, anglerfish at night |
| Clam, starfish, crab, shrimp, octopus | Foraging / fishing | Beach, reef | Clam and starfish on the sand; shrimp and octopus at the reef |
| Worm bait, lure, bobber | Fishing | Dock, inventory | Bait and tackle; better tackle = rarer fish |
| Crab | Story / fishing | Beach | Steals the first catch of the day ("crab tax") |

## E — Tools & gear

| Asset | System | Where | Role |
|---|---|---|---|
| Hoe × 3 tiers | Farming | Farm plots | Till soil; copper tills 3 tiles, iron tills 5 |
| Watering can × 3 tiers | Farming | Farm plots | Water crops; higher tiers hold more |
| Axe × 3 tiers | Gathering | Forest | Chop trees for wood |
| Pickaxe × 3 tiers | Gathering | Rocks, cliff | Mine stone and coal |
| Fishing rod × 3 tiers | Fishing | Dock | Bigger catch window, rarer fish |
| Scythe × 3 tiers | Gathering | Farm, forest | Cut grass for fiber and hay |
| Bucket × 3 tiers | Animals / farming | Barn, well | Carry water, feed animals |
| Hammer × 3 tiers | Crafting / building | Blacksmith, farm | Build and repair (fences, bridge, Counter-Bell) |
| Basic tier | Start | Player's hands | What the player starts with |
| Copper tier | Upgrades | Blacksmith (Ash) | Bought with coins + copper |
| Iron tier | Upgrades | Blacksmith (Ash) | Bought with coins + iron |

## F — Items, materials & food

| Asset | System | Where | Role |
|---|---|---|---|
| Wood, stone, fiber, clay, coal, hardwood, sap, bait | Crafting / gathering | Inventory, shed | Materials for crafting and building |
| Mushroom, wild berry, driftwood, seaweed, shell, herb, flower, feather | Foraging | Forest, beach | Picked up, sold, cooked, gifted |
| Dishes × 12 | Cooking | Kitchen, bakery, inventory | Cooked from crops + fish; restore stamina, gift to NPCs |
| Salad, fish stew, grilled fish, pumpkin soup, berry pie, omelette | Cooking | Kitchen | Early dishes (demo crops + fish) |
| Bread, jam, juice, roast corn, sea soup, cake | Cooking | Bakery, kitchen | Late dishes; cake is a festival gift |

## G — Buildings

| Asset | System | Where | Role |
|---|---|---|---|
| Farmhouse / farmhouse upgrade | Home / day-night | Farm | Sleep, save, store; upgrade adds a kitchen and a second floor |
| Barn | Animals | Farm | Cows, pigs, sheep, goats live here |
| Coop | Animals | Farm | Chickens and ducks live here |
| Silo | Animals | Farm | Stores hay for the animals |
| Stable | Animals | Farm | The horse (travel) — future |
| Well | Story / day-night | Village | The well whispers the player's name at night |
| Lighthouse | Story | Lighthouse point | Burns with no keeper; the Bell's light |
| Dock | Fishing | South-east | Fish here; Bram and Finn stand here |
| Fish shop | Shop | Dock | Sells tackle, buys fish |
| Market stall | Shop | Village | Mia buys produce and fish, sells seeds |
| Bridge | Movement | River / reef | Cross to the reef at low tide |
| Greenhouse | Farming | Farm | Grow any crop in winter |
| Windmill | Cooking | Farm | Grind wheat into flour for bread |
| Shed | Storage | Farm | Stores tools and materials |

## H — Terrain tiles

| Asset | System | Where | Role |
|---|---|---|---|
| Grass × 6 | Terrain | Farm, village, forest | Base ground, varies so the grid does not show |
| Dirt × 4 | Terrain | Paths, farm | Worn ground, walkways |
| Tilled dry × 2, tilled watered × 2 | Farming | Farm plots | Plot states: tilled, watered, planted |
| Sand × 4 | Terrain | Beach | Beach ground |
| Water × 4 | Terrain | Sea, river | Animated water |
| Stone path × 4 | Terrain | Village, farm | Paved walkways |
| Wood floor × 4 | Terrain | Farmhouse, dock, shops | Indoor and pier floors |
| Cliff × 4 | Terrain | Lighthouse point | Cliff edges, not walkable |
| Beach edge × 4 | Terrain | Beach / sea border | Where sand meets water |

## I — Decor & props

| Asset | System | Where | Role |
|---|---|---|---|
| Fence: wood, stone, hedge | Farm / animals | Farm, pens | Keeps animals in, marks the farm edge |
| Scarecrow × 2 | Farming | Farm plots | Protects crops; Pip talks to one |
| Torch × 2 | Lighting | Dock, paths | Light at night |
| Lamp × 2 | Lighting | Village, farmhouse | Light at night |
| Sign × 3 | Navigation | Village, dock, forest | "Farm", "Dock", "Forest" — the player never gets lost |
| Flower pot × 3 | Decor | Village, farmhouse | Decoration, grows one flower |
| Bench × 2 | Decor | Village, well | Sit and rest, talk to NPCs |
| Campfire | Crafting / cooking | Beach, forest | Cook over it, gather round at night |
| Chest × 2 | Storage | Farmhouse, farm | Store items; the second chest is the treasure chest |
| Barrel × 2 | Storage | Dock, farm | Store fish and produce |
| Hay bale × 2 | Animals | Barn, farm | Feed for animals |
| Mailbox | Quests | Farmhouse | Letters from NPCs, quest starts |

## J — UI & screens

| Asset | System | Where | Role |
|---|---|---|---|
| Inventory slots × 4 | Inventory | Inventory screen | Empty, seed, fish, tool slots |
| Hearts × 5 | Friendship | Dialogue, NPC screen | Full, half, quarter, empty — one per friendship level |
| Stamina × 2 | Day/night | HUD | Green bar; drains with every action |
| Clock | Day/night | HUD | Time of day, 6:00–24:00 |
| Season icons × 4 | Seasons | HUD, calendar | Spring, summer, autumn, winter |
| Weather icons × 3 | Weather | HUD | Sun, rain, fog |
| Buttons × 6 | UI | Menus, HUD | Fish, farm, craft, map, inventory, menu |
| Dialogue boxes × 2 | Dialogue | Dialogue screen | Talking to NPCs |
| Menus × 2 | UI | Pause, crafting | Pause menu, crafting menu |
| Title logo | Title | Title screen | "Tidehollow" with lighthouse and waves |

## K — Atmosphere & effects

| Asset | System | Where | Role |
|---|---|---|---|
| Day sky, sunset, night sky | Day/night | Background | The sky changes with the clock |
| Rain | Weather | Overlay | Rainy days, fish bite better |
| Clouds × 3 | Weather | Sky | Drift across the screen |
| Sparkles | Effects | Actions | A good catch, a harvest, a gift |
| Splash | Effects | Fishing | The fish hits the water |
| Bubbles | Effects | Fishing, reef | Underwater, the reef at low tide |

## What the demo uses vs the full game

The demo (Act 1, spring, 7 days) uses: the 4 demo crops, the 5 demo fish,
3 NPCs (Bram, Mia, Finn) + June and Pip, 3 animals (dog, chicken, crab),
5 tools, 8 materials, 5 buildings, 9 terrain tiles, 5 decor items, 7 UI
items, 3 atmosphere items — 103 pictures, one small map.

The full game adds: 8 more crops, 10 more fish, 3 more NPCs (Ash, Rose,
Sol), 7 more animals, tool upgrades, 12 dishes, 7 more buildings, 4 seasons,
weather, crafting, cooking, friendship hearts, quests, the reef, the
greenhouse, the windmill, the Counter-Bell — everything else in this file.
