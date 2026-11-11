# The library to grow

The plan for `library/`: what kinds it holds, the bar an entry must clear, and the first 400 entries in the
order they are made. Each entry is one order for the asset-smith (`docs/WORK_ORDERS.md`, section 11), made one
at a time, judged, then kept for every project. Today the library holds 6.

## 1. The kinds

| Folder | Holds | Example |
|---|---|---|
| `surfaces/` | a surface recipe | `worn_leather` |
| `models/` | one thing | `chesterfield_sofa` |
| `pieces/` | a building part | `stair_straight`, `glazing_wall` |
| `rooms/` | an empty room or shell | `loft_shell` |
| `sets/` | a dressed corner, placed by rules | `reading_corner`, `shelf_lived_in` |
| `plants/` | one plant with its growth numbers | `cholla`, `joshua_tree` |
| `rocks/` | a rock family | `basalt_boulder` |
| `worlds/` | a land preset | `desert_basin` |
| `skies/` | sky, time of day, weather | `clear_desert_morning`, `city_dusk` |
| `lights/` | a light rig | `cove_strip`, `three_point` |
| `people/` | a person preset | `mara_12` |
| `clothes/` | a garment | `linen_shirt` |
| `moves/` | a clip or a set of clips | `walk_relaxed`, `sit_down_chair` |
| `cameras/` | a rig or a shot pattern | `follow_shoulder`, `talk_coverage` |
| `sounds/` | a sound or a living bed | `desert_wind_light` |
| `songs/` | music | `quiet_dusk` |
| `screens/` | a theme | `ember` |
| `kits/` | a game sheet with its sample | `story_walk` |

## 2. The bar (what `loop library check` and the judge enforce)

1. **The card is whole:** what it is, `kind:`, `place:` (one line that works), `credit:`, `licence:`, `from:`,
   and now `tags:`, `size:`, `sockets:`, `seeds:`.
   Two optional lines shape the picture (`loop library picture <name> [--case n]`): `pictures:` up to three
   calls split by `|` (`--case 2` draws the second; with none, the `place:` line), and `view: front | back |
   left | right | below` says where the camera stands (default three-quarter from the back-right; things face
   -z, so `front` stands at -z). The floor is always drawn; a thing tagged steel, iron, dark or black, or one with a
   `view:`, gets a pale wall 3 m behind it so its outline reads.
2. **Our own work or a licence that allows it,** written on the card. No copyrighted designs copied (a sofa
   in the *style* of a chesterfield, never a named maker's product; no logos, no brand names).
3. **True size in metres,** standing on the origin, front towards -z.
4. **Within its size's triangle budget** (hand 500 to 2,000 · chair 3,000 to 10,000 · sofa 10,000 to 30,000 ·
   room 30,000 to 100,000).
5. **At most four surfaces,** all from `library/surfaces/`.
6. **Sockets for what it offers:** `seat`, `hold`, `top`, `hang`, `open`, `light`, `lie`.
7. **Every seed in its range looks right** (three are rendered and judged).
8. **Under about 80 lines of recipe.** Longer means a shape word is missing: report it, do not work round it.
9. **The judge's mark is 7 or more** on surfaces, edges and contact, detail in three sizes, and scale.
10. **It costs nothing when not used,** and its cooked form is kept by fingerprint like every mesh.

## 3. The first hundred: Your Hill (milestone 1)

*Surfaces (10):* decomposed granite · desert pavement · dry wash sand · basalt with varnish · pale granite ·
rust lichen crust · dead wood grey · yucca blade · cactus skin · weathered board.

*Rocks (8):* basalt boulder (pitted, dark) · basalt cobbles · angular granite block · flat slab · scree patch ·
pebble scatter · outcrop ledge · cairn.

*Plants (20):* cholla (three ages) · dead cholla skeleton · Joshua tree (young, forked, old) · Mojave yucca ·
yucca with dead skirt · barrel cactus · hedgehog cactus cluster · creosote bush · broom-like green shrub ·
grey skeleton shrub · dried red ground plant · bunch grass · yellow wildflower drift · tumbleweed ·
Italian cypress · cottonwood · prickly pear.

*Models (40):* a small two-storey house with a flat metal roof · single-wide home · metal shed · quonset
hut · water tank · travel trailer · pickup truck (generic) · power pole and line · fence post and wire · gate ·
mailbox · ladder · solar panel on a frame · propane tank · wheelbarrow · stacked lumber · tyre · oil drum ·
bucket · garden hose coil · plastic chair · folding table · wind chime · weather vane · satellite dish ·
porch awning · wooden steps · cinder block · pallet · tarp over a pile · dog bowl · clothes line · fire ring ·
bench · sign post · antenna mast · swamp cooler · well cap · shipping container · carport.

*Worlds, skies, lights, sounds (12):* desert basin · clear desert morning · desert noon · desert golden hour ·
desert dusk · desert night with stars · a hard sun rig · desert wind light · desert wind gusting · grit
footsteps · far dog and bird bed · a quiet morning song.

*Sets (10):* a rocky slope patch · a wash crossing · a yard round a house · a porch · a junk corner · a garden
square · a road verge · a hilltop cairn · a cactus garden · a shed interior.

## 4. To four hundred: The Loft (milestones 3 and 4)

*Pieces (40):* loft shell (double height) · brick wall bay · glazing wall with thin bars · steel beam ·
timber joist ceiling · mezzanine deck · straight stair with open treads · spiral stair · glass balustrade ·
steel rail · cove for a light strip · concrete floor · timber floor · door and frame · sliding door ·
kitchen run (base, wall, tall) · island · worktop · splashback · extractor · built-in shelves · window seat ·
skirting · ceiling track · column · duct · radiator · fireplace · chimney breast · roof light · bathroom
pod · shower screen · wardrobe wall · bed platform · step with a light under · niche · plinth (have) ·
lintel (have) · room shell (have) · curtain track.

*Seating and beds (25):* deep modular sofa (corner, straight, chaise) · chesterfield-style sofa · armchair ·
lounge chair and footstool · pouffe · floor cushion · bean bag · dining chair (wood, metal, upholstered) ·
bar stool (three) · bench · office chair · gaming chair · bed (low platform, framed) · mattress ·
headboard · bedside table · daybed · window bench cushion.

*Tables and storage (25):* coffee table (wood slab, round glass, trunk) · side table · dining table (two) ·
desk · console · TV unit · bookshelf (open, cube, ladder) · display cabinet with glass · sideboard ·
chest of drawers · wardrobe · shoe rack · coat stand · trolley · plant stand · wall shelf (three lengths) ·
picture ledge · crate · basket.

*Soft things (30):* cushions (square, bolster, round; six covers) · throw (folded, draped) · knitted blanket ·
duvet (made, rumpled) · pillows · shag rug · flat-weave rug · sheepskin · runner · curtains (open, closed,
sheer) · blind · towel (hung, folded) · bath mat · table runner · napkin · apron · jumper on a chair.

*Lamps (25):* cove strip · step strip · pendant (dome, globe, cage, cluster of three) · ring chandelier ·
crystal chandelier · floor lamp (arc, tripod, reading) · table lamp (four) · bedside sconce · picture light ·
spotlight on a track · string lights · candle (three) · lantern (have) · neon sign (generic words) ·
screen glow (TV, monitor, laptop) · fridge light · hob flame.

*Kitchen and dining (50):* fridge · oven · hob · sink and tap · kettle · toaster · coffee machine ·
microwave · mixer · pans (three) · pots (three) · knife block · chopping board · utensil pot · plates ·
bowls · mugs · glasses (wine, tumbler, tall) · cutlery · jars (six) · bottles (wine, oil, water) · fruit
bowl with fruit · bread · tea towel · spice rack · paper towel · dish rack · bin · cookbook stack · tray ·
vase with flowers · candle holder · salt and pepper · teapot · jug · cake under a dome · cheese board ·
grapes · snack bowl · pizza box · takeaway cup · water carafe · wine rack · bar tools · ice bucket ·
cocktail glass · decanter · coaster · placemat · serving bowl.

*Shelf things (60):* books (runs of 5, 7, 9; laid flat; open; one face-down) · magazines · records and a
player · speakers · headphones · camera · globe · clock (wall, desk, mantel) · picture frames (six sizes) ·
canvases (abstract, landscape, portrait, each our own) · mirror (round, leaning) · sculptures (five) ·
bowls and vases (ten profiles) · boxes · candles · trophies · toy figures (our own) · board game boxes
(generic) · chess set · jars of things · storage baskets · binders · pen pot · desk lamp · keyboard · mouse ·
monitor · laptop · games controller (generic) · console (generic) · remote · router · phone on a stand ·
tablet · plant pots (ten) · terrarium · incense holder · snow globe · hourglass · model ship (the sailing
boat, small) · model car · ukulele · guitar on a stand · skateboard · helmet · binoculars · maps rolled ·
sewing box · yarn basket · umbrella stand · keys in a dish.

*Plants indoors (20):* fiddle-leaf fig · monstera · snake plant · pothos trailing · rubber plant · olive
tree in a pot · palm · fern · succulents (five) · cactus in a pot · herbs on a sill · hanging basket ·
orchid · bonsai · bamboo · dried grasses in a vase · peace lily · ivy on a shelf · moss ball · bouquet.

*Bathroom and bed (15):* toilet · basin · mirror cabinet · bath · shower head · towel rail · bottles ·
toothbrush cup · laundry basket · scales · robe on a hook · slippers · alarm clock · book and glasses ·
phone charger.

*Skies and outside (10):* night city in layers · city at dusk · sunset over forested hills · overcast ·
rain on glass · a balcony · neighbouring brick wall with lit windows · rooftop water towers · street
glow from below · moon.

*Sets (20):* reading corner · TV wall · sofa group · dining for six · breakfast bar · cook's counter ·
bed made · bed slept in · desk at work · gaming desk · shelf lived in · gallery wall · plant corner ·
entry with shoes and coats · bar cart · record corner · window seat with cushions · bathroom shelf ·
stair with things on the steps · a cat's corner.

*People and creatures (5, with track P):* a cat (sitting, lying, walking) · a dog · a person seated ·
a person cooking · two people on a sofa.

## 5. To a thousand and on

By need, as each star and kit asks: The Valley's plants, rocks, river stones, ruins and terraces (150);
people, clothes and hair (120); moves (200); cameras and light rigs (40); sounds and songs (120); themes
(10); kits (16); then each new project's own additions, which is the rule that already exists: whatever a
goal makes that another project could use goes into the library.

## 6. How an entry is made (the asset-smith's steps)

1. Read the card of the order and two existing entries of the same kind.
2. Find real measurements (from common knowledge of standard sizes; `docs/CRAFT.md` section 4 has the usual
   ones). Write them at the top of the recipe as named numbers.
3. Block the silhouette with the fewest shape words. Render it. Compare with the description.
4. Add the medium parts, then the small. Round every edge.
5. Give it surfaces by name, sockets, a seed range.
6. `loop library check`, three seeds' pictures, then the judge. Fix the judge's one change; at most three rounds,
   then report honestly with the mark it reached.
