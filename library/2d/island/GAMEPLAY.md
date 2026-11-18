# Tidehollow — how the whole game plays, start to finish

The design doc for the game in `STORY.md`, using the sprites in `SPRITES.md`
and the art scope in `PLAN.md`. One rule above all the others: **cozy first,
mystery second, never punishing.** The spooky stuff is atmosphere and story,
never a fail state. The player should always feel like one more day is a good
idea.

## The promise — what the player should feel

- **Morning:** "What am I going to do today?" A small farm, a dock, a whole
  island, and a list of things that are entirely theirs.
- **Midday:** the cozy loop working — dirt under the hoe, a fish on the line,
  a villager with something funny to say.
- **Night:** the island is wrong in a beautiful way. The well knows your
  name. The lighthouse burns with no keeper. You want to know why.
- **Over weeks:** the mystery assembles itself, one conversation and one
  quest at a time, until the final night at the lighthouse — and the choice
  is yours.

## Controls — touch-first, one thumb

The game is built to be played on a phone, one-handed, in short sessions.

- **Move:** virtual joystick, left thumb. Tap-to-move also works: tap the
  ground and the farmer walks there.
- **Act:** one big action button, right thumb. It does the right thing for
  whatever is in front of you: hoe the dirt, plant the seed, water the crop,
  pick the fish up, talk to the person, open the chest. One button, no menus
  in the way.
- **Tool switch:** swipe left/right on the action button, or tap the tool
  belt at the bottom. The game auto-selects the sensible tool most of the
  time.
- **Inventory / menu:** one tap, big tiles, readable on a small screen.
- **No fail inputs:** there is no jump timing, no combat, no reaction test
  except the fishing minigame — and even that can be set to easy.

## The first 15 minutes — the hook

The first quarter-hour decides whether the player stays. It is scripted
tightly, then the game opens up.

| Minute | What happens | Why it works |
|---|---|---|
| 0:00 | Fog. A boat. A letter read aloud: *"The farmhouse is yours."* The player steps onto the dock. | Mystery in the first 10 seconds, no menus first. |
| 0:30 | Pip is waiting on the dock. "You're the new farmer! The scarecrow says hi." He walks the player home. | A friendly face immediately; the weird (talking scarecrow) is introduced as cute, not scary. |
| 2:00 | The farmhouse. June is there with a key and a basket: 4 seed packets, a hoe, a watering can. "Your grandfather's farm. He'd be glad it's you." | The player gets tools, seeds, and the first story hook in one gift. |
| 4:00 | First farming: hoe 4 plots, plant 4 seeds, water them. Each action has a sound, a particle puff, a little bounce. The seeds sprout on screen. | The core loop is taught by doing, in under 2 minutes, and it *feels* good. |
| 7:00 | Finn at the dock: "First catch is free." The fishing tutorial — one cast, one fish, a little fanfare. | Second core loop, same pattern: do it once, feel great. |
| 10:00 | The market stall: sell the fish, get the first coins, the coin sound. Mia: "You're open for business." | The loop closes: work → money → progress. |
| 12:00 | Free play begins. A short to-do list appears: meet the villagers, explore the island, sleep when ready. | The player is free, but never lost. |
| 15:00 | Night falls. The lighthouse light sweeps the farm. The well whispers the player's name. Pip's voice: "Don't worry. It does that to everyone." Sleep in the farmhouse bed → Day 2. | The spooky hook lands *after* the cozy loop is learned. The player goes to sleep wanting to know more. |

## The first week — one new thing a day

Never dump systems. One new thing per day, each one a small delight.

- **Day 1 (spring):** farm 4 plots, fish once, meet Pip and June, sleep.
- **Day 2:** the shop opens — buy seeds, sell produce. Meet Mia. The ledger
  is mentioned, not explained.
- **Day 3:** animals — feed the chicken, get the first egg. Meet Finn
  properly. The dog follows the player home.
- **Day 4:** foraging — the beach, the berry bush, the tide pools. Meet Bram.
  He mentions the fog.
- **Day 5:** crafting — build a chest, a scarecrow, a fence. The scarecrow
  is facing the sea in the morning. Pip says not to worry about it.
- **Day 6:** the first quest board — small jobs for villagers (bring Rose
  3 wheat, find Sol's lost compass). First friendship heart.
- **Day 7:** the first moonless night. The Drowned Bell rings. The player's
  name is inside the ringing. Act 1 ends. The player now knows the game is
  not just a farm.

After day 7, the game stops teaching and starts trusting the player.

## The core loops

### The day loop (10–15 minutes of play, in-game 6am–midnight)

1. Wake up. Check the weather, the mail, the crops.
2. Tend the farm: water, harvest, replant. (3–5 minutes.)
3. Pick a goal for the day: fish for money, forage, do a quest, explore,
   talk to someone. (5–8 minutes.)
4. Evening: sell, cook, craft, visit the stall or the shop. (2–3 minutes.)
5. Sleep. Overnight the crops grow, the mail arrives, the island moves one
   small step. The day summary shows what the player earned and what grew —
   the "one more day" screen.

### The season loop (28 days per season, 4 seasons)

- **Spring:** arrival, learning, Act 1. Everything is new and green.
- **Summer:** the bell, the mystery deepens, Act 2. Long days, big catches.
- **Autumn:** the harvest, the Pale Tide, Acts 3–4. The story accelerates.
- **Winter:** short days, the longest night, Act 5, the choice.
- Each season has its own crops, fish, forage, weather, and one seasonal
  event: spring planting fair (small, the first in 60 years), summer night
  fishing, the Pale Tide in autumn, the longest night in winter.
- Seasonal change is visible everywhere: trees change colour, the light
  changes, the villagers wear different clothes, the music shifts.

### The year loop (the whole game)

- Year 1 is the story: five acts, three endings.
- After the ending, free play continues — the farm, the fishing, the
  villagers — plus New Game+ style completion: all fish, all crops, all
  hearts, all eight pages of the truth, the secret ending.

## The systems, in depth

### Farming — the heart

- Till soil, plant seeds, water daily, harvest. Crops grow in 4 visible
  stages (seed, sprout, growing, mature) — the sprites in `SPRITES.md`.
- Watering matters: a watered crop grows overnight; a dry one waits. No
  crop ever dies — a dry crop just pauses. No punishment, only patience.
- 12 crops across the year, 4 per season-ish, each with a season it loves.
  Pumpkins grow biggest near the old festival field — the field remembers
  the feast.
- Quality: a small chance of a silver or gold crop, worth more, loved as
  gifts. Fertilizer (from the compost bin) raises the chance.
- Scarecrows protect crops from crows. Crows are also how the game tells
  the player the scarecrow moved — a crow sitting on it, facing the sea.

### Fishing — the thrill

- Cast at the dock, the beach, the tide pools, the north cliff. Different
  water, different fish.
- Minigame: a fish bites, a bar appears, keep the marker on the fish. Easy
  by default; the Kingfish is the one hard catch in the game, and it only
  appears on the night of the Pale Tide.
- 15 fish + 5 sea creatures. Each has a season, a time of day, and a place.
  The journal records them all — completionists get a full page.
- The white fish whisper names when caught. Never eat one. (If the player
  tries, the screen fades, the player wakes up in bed, and June says
  "told you." No other penalty.)
- The crab steals the player's first fish of the day, every day, and drags
  it to the tide pools. It is collecting, the way the tide collects.

### Animals — the warmth

- Chicken (eggs), cow (milk), pig (truffles), sheep (wool), goat (milk),
  duck (eggs), dog and cat (companions, no product — they follow the player
  and sleep by the fire).
- Feed, pet, and they get happier; happy animals give better produce.
  Animals have names, and the villagers comment on them.
- The dog finds things: dig spots, lost items, the occasional coin from a
  ship that sank in 1887.

### Foraging, mining, crafting — the side loops

- Forage the beach, the woods, the tide pools: berries, mushrooms,
  driftwood, shells, herbs. Different things each season.
- The reef at low tide: iron, coal, and the bell-metal for Ash's
  Counter-Bell. Low tide is a time window — miss it, come back tomorrow.
- Crafting: chests, fences, the scarecrow, the compost bin, the kitchen,
  the boat upgrade. Recipes come from villagers and from the shop.
- Cooking: 12 dishes. Dishes restore more stamina than raw food, and
  villagers love them as gifts. Rose teaches the recipes — including, at
  the end, the Loaf of Returning.

### Money and the shop

- Sell produce and fish at Mia's market stall; buy seeds, tools, furniture,
  and recipes at the shop.
- Money is for growth, not survival: seeds, upgrades, the farmhouse
  extension, the boat. Nothing the player *needs* is ever locked behind a
  grind.
- Tool upgrades (basic → copper → iron) come from Ash: faster hoeing,
  bigger watering, stronger rod. Each upgrade is a small quest, not just a
  price tag.

### Villagers and friendship — the soul

- 8 villagers, each with a daily routine, a favourite gift, and a 3-quest
  chain. Talk to them every day; hearts fill up.
- Hearts unlock: new dialogue, new quests, new recipes, new areas, and —
  most importantly — their page of the truth. Max hearts with all eight is
  required for the full story and the secret ending.
- Villagers talk about each other, about the weather, about the player's
  farm. After the player does something notable (first gold crop, the
  Kingfish, the bell), the whole village comments on it the next day.
- Nobody is a quest dispenser. Each chain is a small story: Bram's ship,
  Finn's brother, Mia's ledger, Ash's bell, Rose's loaf, Sol's crate, Pip's
  scarecrow, June's forty places.

### Quests — the story engine

- **Main quests (5 acts):** one at a time, always clear, always pointing
  at the next story beat. The journal says what to do and why.
- **Villager chains (8 × 3 = 24 quests):** the meat of the game. Each chain
  ends with a page of the truth.
- **Daily jobs:** small tasks on the quest board — fetch, deliver, catch —
  for money and friendship. Never required, always available.
- **Hidden quests:** things the player finds, not things they're told: the
  letters in the farmhouse wall, the photograph the crab stole, the recipe
  in the lighthouse log. The game never marks these. Finding one feels like
  a secret.

### Time, stamina, and saving — the guardrails

- **Time:** a day runs 6am to midnight. The clock is always visible but
  never stressful. At midnight the player gets sleepy; at 2am they fall
  asleep where they stand and wake up in bed. No penalty, just a lost hour.
- **Stamina:** actions cost a little. Eating restores it. Running out means
  the player can't work until they eat or sleep — never a fail, just a
  reason to go to the kitchen or the bakery.
- **Saving:** autosave every night and after every quest. The player can
  also save anywhere from the menu. Sessions are 10–15 minutes and the game
  respects that — quitting mid-day resumes exactly where the player left off.

### Exploration — the island itself

- The island is small enough to cross in a minute, dense enough to hide
  things: the tide caves (low tide only), the reef, the north cliff, the
  festival field, the church with no priest, the lighthouse (locked until
  Act 3).
- New areas open as the story opens them, not as the player grinds. The
  lighthouse opens when Mia's page points there. The tide caves open when
  Pip trusts the player enough to show them.
- Secrets reward curiosity, not completion: the photograph, the letters,
  the compass, the drawing of the sea. None are marked on the map.

## How the story and the gameplay fit together

| Season | Act | What the player does | What the player learns |
|---|---|---|---|
| Spring | Act 1 — Arrival | Farm, fish, meet everyone | The island feeds you. The well knows your name. |
| Summer | Act 2 — The bell rings | First moonless night, the bell, the scarecrow moves | The Tithe is real, and it is due this year. |
| Autumn | Act 3 — The ledger and the log | Mia's page, Bram's log, Finn's Kingfish | The debt is almost paid. One more Tithe — or one willing gift. |
| Late autumn | Act 4 — The Pale Tide | The white tide, the Loaf, the Counter-Bell, the tide caves | The Tithed are kept, not dead. The bell can be answered. |
| Winter | Act 5 — The choice | Midnight at the lighthouse, ring back | Three endings. The player chooses. |

The rule: **the main quest never advances without the villagers.** Each act
needs the pages around it. A player who rushes the main quest hits a locked
door and a villager who says, gently, "You're not ready. Talk to June first."
The game never punishes rushing — it redirects it into the good stuff.

## Making it the best experience — the details that matter

### Feel (juice)

- Every action has a sound, a particle, and a tiny bounce: the hoe thunks,
  the water sparkles, the fish splashes, the coin *chings*. Nothing in the
  game is silent or still.
- Harvesting a mature crop pops it off the plant with a little hop. A gold
  crop sparkles and plays a higher chime.
- The fishing catch has a 1-second slow-down and a splash — the best moment
  in the game, every time.
- Rain on the roof, the lighthouse beam sweeping the farm at night, fireflies
  in summer, snow in winter. The island is alive even when the player is
  standing still.

### Sound and music

- A soft acoustic loop by day that changes per season; a slower, stranger
  version at night — same melody, minor key, a bell underneath.
- The Drowned Bell is a real sound the player hears before they understand
  it. The first time it rings, the music stops. That silence is the scare.
- Villager voices are not words — soft blips, each with their own pitch.
  June is low and slow. Pip is fast and high. Sol talks too much.

### Pacing — the "one more day" machine

- Every night, the summary screen shows: money earned, crops grown, a
  villager's comment, and one teaser for tomorrow ("The tide will be low
  at dawn." "Mia has something to show you."). The player goes to sleep
  with a reason to wake up.
- Something new every day for the first two weeks; something new every week
  after that. A new fish, a new recipe, a new area, a new line of dialogue.
  The game never has a dead day.
- Quests are short (one day each, mostly). Chains are long (a season).
  The story is a year. Every scale of goal is always in reach.

### No punishment, ever

- Crops never die. Fish never run out. The player never loses money, never
  loses a day, never fails a quest — a quest can only wait.
- Passing out just wakes the player in bed. Eating a white fish just wakes
  the player in bed, with June saying "told you."
- The scary stuff is story and atmosphere, never a jump-scare or a fail
  state. A 9-year-old can play it. An adult finds the depth.

### Surprise and delight

- The crab steals the first fish of the day. The dog digs up a coin from
  1887. The scarecrow faces the sea some mornings. Sol's stories are all
  wrong in a funny way. Pip names the player's crops.
- The villagers remember: if the player gives June a flower on day 3, she
  mentions it on day 30.
- The festival field: if the player sets forty places there (June's quest),
  fireflies gather over them on the longest night. No explanation. Just
  beautiful.

### Accessibility

- Easy fishing mode (bigger bar, slower fish). Text size options. No
  colour-only information — every important thing has a shape or a label
  too. No timed dialogue. The game can be played entirely at the player's
  own pace, and the story waits for them.
- The whole game is playable in 10-minute sessions, one-handed, on a phone.

## Endgame and replayability

- **After the ending:** free play continues — farm, fish, villagers, seasons.
  The island keeps living. The ending changes it: after Break the Bell, the
  lighthouse is dark and the fog is gone; after Keep the Bargain, the light
  burns gold and the fog stays; after Give Your Name, the villagers treat
  the player like family and the player can't remember why that feels new.
- **100% completion:** all 15 fish, all 12 crops, all 8 villagers at max
  hearts, all 8 pages of the truth, all 12 dishes, the Kingfish, the secret
  ending. The completion screen is June's photograph, with the player's
  face added to it.
- **Replayability:** the three endings, the villager chains in any order,
  and the farm itself — no two farms look the same.

## What the demo covers vs the full game

| | Demo | Full game |
|---|---|---|
| Time | Spring only, ~7 days | Full year, 4 seasons |
| Story | Act 1 (arrival) | All 5 acts, 3 endings |
| Systems | Farm, fish, sell, 2 NPC chats, feed 1 animal, save | Everything above |
| Map | The farm, the dock, the stall | The whole island, caves, reef, lighthouse |
| Art | 103 pictures | 361 pictures |

The demo is Act 1: the first 15 minutes, the first week, the first moonless
night. If the demo works, the player has felt the cozy loop *and* heard the
bell — and they want the rest of the year.
