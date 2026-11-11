# Surface recipes

The design behind track M of `docs/ROADMAP.md` (order M1). Decided 2026-11-10; nothing here is built yet.
It fixes the word, the layers, what the cook measures and bakes, and what the phone does per pixel, so that
orders M2 to M62 never have to decide them. Builders: follow this page; if it is silent or wrong, stop and
tell the lead.

Contents: 1 the idea · 2 the decisions · 3 the word · 4 the layers · 5 the patterns · 6 the marks ·
7 how a point is worked out · 8 what the cook bakes · 9 what the phone does · 10 projection ·
11 the library entry · 12 the seam in code · 13 what it does not do · 14 which order builds what

---

## 1. The idea

Today a material is one colour, one roughness and one of nine flat patterns (`engine/render/pattern.cpp`),
each written twice: once in C++ for the film renderer and once in the shaders for the phone. Nothing knows
where a thing's edges or creases are, so nothing wears and nothing gathers dirt. That is why every scene looks
like plastic (`docs/CRAFT.md`, section 3: nothing is one colour; roughness matters more than colour; edges
catch light; dirt goes where hands and rain can't).

A **surface recipe** is a short text that says what a surface is made of, in layers, in real sizes. From one
recipe:

- the **film renderer** works out every point exactly (the recipe is a function);
- the **cook** bakes the part that repeats into small pictures, once, kept by the recipe's fingerprint;
- the **phone** reads those pictures and adds the three things that cannot repeat: slow change across metres,
  wear on this thing's edges, dirt in this thing's creases.

One source, two pictures, as everywhere in the engine.

## 2. The decisions

1. **A recipe is a fixed stack of named layers,** always worked out in this order: `base`, `vary`, `coat`
   (up to two), `wear`, `dirt`, `detail`. Not a free list of anything over anything: a fixed stack can be
   checked, baked and drawn on a phone at a known cost, and fifty of them read alike.
2. **One word, `surface`,** makes one. Each layer is a map of settings. A surface is a material: every shape's
   `material:` takes it.
3. **Sizes are real.** Every pattern says the size of its feature in metres or millimetres (a brick, a pore,
   a thread). No "scale" numbers.
4. **Patterns live in one place** (C++), never again in a shader. The phone reads baked pictures, so a new
   pattern costs the phone nothing.
5. **The repeating part is baked as a square tile;** the recipe says the tile's side (`tile: 2 m`). Every
   pattern wraps at the tile's side, in the film renderer too, so film and phone show the same surface.
6. **What depends on the thing is not baked.** Edges and creases are measured once per mesh by the cook, two
   numbers a vertex (the **marks**, section 6). Wear and dirt are worked out from them where the surface is
   drawn. One baked oak serves every oak thing; each wears on its own edges.
7. **Slow change is not baked either:** one shared small picture of smooth noise (the **wander picture**) is
   read at a scale of metres, so a floor never shows its tile repeating.
8. **Fine bumps under two metres** are a second, small tile (`detail`), faded out with distance.
9. **Projection is a property of the surface:** `box` (from three sides, the default), `top` (from above:
   land), or `uv` (along the mesh's own unwrap). In the thing's own space for things that move, in world
   space for land and building parts, so neighbours match across their seams.
10. **Colours are true.** A non-metal's base brightness is between 0.03 and 0.9 (`docs/CRAFT.md`, section 3);
    a layer is a metal or it is not (`metal: 0` or `1`); `loop library check` refuses a recipe outside this.
11. **`material("oak")` finds a surface first.** Order: a surface made in this scene, then
    `library/surfaces/oak.loop`, then the 59 built-in flat materials. So old scenes get the new oak the day
    its entry lands, and the flat list retires name by name.
12. **Glass, glow, water, skin, eyes and hair are not surface recipes.** They keep their own material
    settings (M60 and track P).

## 3. The word

```
surface("oak",
  tile: 2 m, project: "box", along: "long",
  base:   {color: rgb(0.42, 0.27, 0.14), color2: rgb(0.27, 0.16, 0.08), roughness: 0.55, roughness2: 0.7,
           pattern: "grain", size: 6 mm, stretch: 40, bump: 0.15 mm},
  vary:   {large: 1.2 m, amount: 0.12, tint: rgb(0.50, 0.30, 0.13), roughness: 0.08,
           small: 5 cm, small_amount: 0.05},
  wear:   {color: rgb(0.55, 0.38, 0.22), roughness: 0.35, amount: 0.8, ragged: 0.5},
  dirt:   {color: rgb(0.10, 0.07, 0.05), roughness: 0.85, amount: 0.6, ragged: 0.6},
  detail: {pattern: "pores", size: 0.4 mm, stretch: 6, bump: 0.03 mm, tile: 8 cm})
```

It gives back a material, so `box size: vec(1.6, 0.06, 0.9), material: surface("oak", …)` works, and so
does `material: "oak"` once the entry is in the library. Every layer except `base` may be left out. An
unknown layer or setting is refused by name, with the nearest right one offered, as `material` does today.

Settings beside the layers:

| Setting | Means | Default |
|---|---|---|
| `tile` | the side of the square that repeats | `1 m` |
| `project` | `"box"`, `"top"` or `"uv"` (section 10) | `"box"` |
| `space` | `"thing"` (moves with the thing) or `"world"` (neighbours match) | `"thing"` |
| `along` | which way a stretched pattern runs: `"long"` (the mesh's longest side), `"x"`, `"y"`, `"z"` | `"long"` |
| `seed` | another piece of the same surface (a library entry's card says its range) | `1` |
| `sheen`, `clearcoat`, `subsurface`, `subsurface_color`, `translucent`, `ior` | as on `material`, for the whole surface | as `material` |

Two things with the same surface never show the same piece of it: each thing starts at its own place in the
tile (from its own number), which costs nothing.

## 4. The layers

**`base`** · what it is made of. Two colours and two roughnesses mixed by one pattern.

| Setting | Means |
|---|---|
| `color`, `roughness` | where the pattern is 0 (the brick, the early wood, the thread) |
| `color2`, `roughness2` | where the pattern is 1 (the mortar, the late wood, the gap between threads); default: the same |
| `metal` | 0 or 1, the whole layer |
| `pattern`, `size`, `stretch`, `contrast`, `sharp` | section 5 |
| `bump` | the pattern's height from lowest to highest, in mm |

**`vary`** · nothing is one colour. Never baked large; baked small.

| Setting | Means |
|---|---|
| `large` | the size of the slow change (0.5 m and more); read from the wander picture where drawn |
| `amount` | how much brighter and darker it goes (0.1: a tenth either way) |
| `tint` | the colour it leans towards where the change is high |
| `roughness` | how much the shine wanders (added and taken away) |
| `small`, `small_amount` | a finer mottle (centimetres), baked into the tile |

**`coat`**, **`coat2`** · a second material over part of the first: varnish on stone, moss, rust, snow, old
paint, a stain.

| Setting | Means |
|---|---|
| `color`, `roughness`, `metal` | what the coat is |
| `cover` | the share it covers, 0 to 1 |
| `pattern`, `size`, `stretch`, `sharp` | where it lies (default `"noise"`) |
| `sits` | `"high"` (on the base's raised parts), `"low"` (in its hollows) or `"any"` |
| `facing` | `"up"`, `"down"`, `"side"` or `"any"`: only where the thing faces that way |
| `bump` | its thickness in mm |

A coat with `facing: "any"` is baked into the tile. A coat with a facing is worked out where drawn, because
it depends on the thing; a surface may have one of those.

**`wear`** · edges catch light. Worked out where drawn, from the mark `edge`.

| Setting | Means |
|---|---|
| `color`, `roughness`, `metal` | what shows where it is worn (paler wood, shinier leather, bare metal under paint) |
| `amount` | 0 none … 1 every true edge worn through |
| `ragged` | 0 a clean line … 1 broken up by the surface's own height (high points wear first) |
| `soft` | how gradual the border is (default 0.2) |

**`dirt`** · goes where hands and rain can't. Worked out where drawn, from the mark `hollow`.

| Setting | Means |
|---|---|
| `color`, `roughness` | the dirt (dark and dull as a rule; pale and dull for dust) |
| `amount`, `ragged`, `soft` | as `wear` (low points fill first) |

**`detail`** · the fine bump seen under two metres: weave, pores, grit, brushing.

| Setting | Means |
|---|---|
| `pattern`, `size`, `stretch` | section 5 |
| `bump` | its height in mm |
| `roughness` | how much it changes the shine between its high and low |
| `tile` | the side of its own small square (5 to 50 cm) |
| `fade` | the distance by which it has gone (default `2 m`) |

## 5. The patterns

A pattern is a pure function of a place on the tile (two numbers, in metres) and the seed. It gives three
numbers: `mix` (0 to 1: which of the two colours), `height` (0 to 1) and `piece` (a number that is the same
across one brick, plank, chip or cell, for a small change of tone from piece to piece). All wrap at the
tile's side: the cook fits a whole number of features into the tile by changing `size` by a few per cent at
most, and says so if it had to change it by more than 5%.

| Pattern | Its feature (`size` is…) | For |
|---|---|---|
| `noise` | one soft blob | stains, moss, clouded stone |
| `speckle` | one grain | granite, grit, terrazzo fines, sand |
| `cells` | one cell | pitted basalt, gravel, chips, boucle loops, cobbles |
| `pores` | one pore, sparse | leather, wood pores, concrete air holes |
| `cracks` | one plate between cracks | dried mud, old paint, glaze |
| `veins` | the gap between veins | marble |
| `strata` | one bed's thickness | sandstone, slate, layered rock |
| `grain` | one growth ring's width | every wood (`stretch` along the grain) |
| `planks` | one board's width (`stretch`: its length) | floors, decking, cladding |
| `bricks` | one brick's length (`stretch`: length over height; a 10 mm joint) | brick, block, stone courses |
| `tiles` | one tile's side (a joint of `sharp`) | ceramic, pavers, panels |
| `weave` | one thread's width (`twill: 2` steps the cross-over) | linen, canvas, denim |
| `knit` | one stitch | wool knit |
| `brushed` | one scratch's width (`stretch`: its length) | brushed steel, sanded wood |
| `checker`, `grid` | one square | test floors |

Shared settings: `size` (true size), `stretch` (length over width, 1 by default), `contrast` (0 to 2: how far
from the middle `mix` goes), `sharp` (0 soft … 1 a hard border), `piece` (0 to 0.3: tone change from piece to
piece). The nine old pattern names keep working on `material(…, pattern: …)` until the flat list has retired.

A pattern that is missing is a card for the lead, never something a surface's maker works round.

## 6. The marks (what the cook measures on a mesh)

Two numbers a vertex, a byte each, measured once per mesh and kept by the mesh's fingerprint beside its
cluster tree:

- **`edge`** (0 to 1): how much this vertex stands out. 1 on a rounded edge or a bump of 1 cm radius or
  tighter; near 0 on anything flat or as gently curved as a football.
- **`hollow`** (0 to 1): how much it is tucked in. 1 in a crease or a fillet of 1 cm radius or tighter, or
  where most short rays from it hit the same mesh (the inside of a cup, between sofa cushions); 0 in the open.

**The rule that makes them safe to blend across a triangle:** a mark is lowered by how big the triangles
round the vertex are. A vertex on the border of a large flat face gets 0, however sharp the edge beside it,
so wear lives on the narrow band of the rounding and never spreads across the face. It follows that **a
knife edge gets no wear and no dirt**: a thing must round its edges (`round:` and `bevel:` in three steps,
order S5) to wear. That is the same rule as `docs/CRAFT.md` section 4, "every edge rounded".

How (order M2): weld by position; `k` = the sharpest bend through the vertex, in 1/metres, with its sign
(out or in); `edge = clamp(k × 1 cm)` and `hollow = clamp(-k × 1 cm)`; `hollow` is raised to the share of 16
rays of 10 cm that hit the same mesh; each is then multiplied by `clamp(2 cm / a)` for the largest `a`,
where `a` is the smallest height of a triangle at that vertex. The numbers 1 cm, 10 cm and 2 cm are fixed
here; the tests in M2's card say what must come out.

Marks cross from one thing to another nowhere: a cup on a table darkens the table through the renderer's
own ambient occlusion, not through dirt.

On land the same two bytes carry the blend of the ground's three layers instead (order W5).

## 7. How a point is worked out

The same steps in the film renderer (exactly) and on the phone (steps 1 to 3 read from the baked tile). `u`
is the place on the tile from the projection, `n` the thing's own facing there, `edge` and `hollow` the
marks blended across the triangle, `w` the wander picture read at `u / large` (four smooth noises, each -1
to 1).

1. **Base.** `(mix, height, piece)` from the base's pattern at `u`. Colour and roughness: between the two by
   `mix`; colour times `1 + piece`. `h = height × bump`.
2. **Small change.** Colour times `1 + small_amount × noise(u / small)`.
3. **Baked coats** (`facing: "any"`), in order. `c` from the coat's pattern; lean it by `sits`
   (`c + (height - 0.5)` for high, `c - (height - 0.5)` for low); `m = step(1 - cover, c)` softened by
   `sharp`. Colour, roughness and metal move to the coat's by `m`; `h` rises by `m × bump`.
   *Here the tile ends:* colour, `h`, the bumped facing from `h`'s slope, roughness, metal.
4. **Large change.** Colour times `1 + amount × w1`, then towards `tint` by `amount × max(w2, 0)`; roughness
   plus `roughness × w3`.
5. **The coat that faces.** `f` from `n` (`up`: `clamp((n.y - 0.3) / 0.4)`; `down`: the same with `-n.y`;
   `side`: `1 - |n.y|`); `t = f × cover × 2 × (1 + (b - 0.5) × 2 × ragged)` where `b` is the tile's height
   (or one less it, for `sits: "low"`); `m = smoothstep(0.5 - soft / 2, 0.5 + soft / 2, t)`. Blend as step 3.
6. **Wear.** `t = edge × amount × 2 × (1 + (b - 0.5) × 2 × ragged)` with `b` the tile's height; `m` as in
   step 5. Colour, roughness and metal move to the wear's by `m`.
7. **Dirt.** The same with `hollow` and `b` one less the tile's height. Dirt lies over wear.
8. **Detail,** within `fade`: its bump tilts the facing; its `roughness` is added between its low and high;
   faded to nothing by `fade` and wherever one pixel covers more than half a feature.
9. Roughness is kept between 0.03 and 1. What comes out is what `shadeSurface` gives today: colour,
   roughness, metal, the bumped facing.

A mark of 0 always gives no wear and no dirt, whatever the other numbers.

## 8. What the cook bakes

Per surface and seed used in a scene, kept by the recipe's fingerprint (its text's numbers and the picture
size), made on every core like cluster trees:

| Picture | Holds | Size |
|---|---|---|
| **colour** | red, green, blue: the colour after step 3 (sRGB); alpha: the height `h`, 0 to 1 | `render.textureSize` square |
| **shape** | red, green: the bumped facing's two sideways parts; blue: roughness; alpha: metal | the same |
| **detail** (if the recipe has one) | red, green: its bump's facing; blue: its roughness change (0.5: none); alpha: its height | 128 square |

And once for the whole engine: the **wander picture**, 64 square, four smooth noises that wrap.

- Each picture gets its full chain of smaller copies. In the smaller copies the facing is averaged and made
  whole again, and **roughness is raised by how much the bumps disagreed** (so a far surface is duller, not
  sparkling: Toksvig's rule).
- The bake runs steps 1 to 3 at each pixel's centre, four samples a pixel.
- **What the eye needs:** hand-sized things about 2,000 pixels a metre, chair-sized 1,000, sofa-sized 500,
  room-sized 250, land 60. A 2 m tile at 256 gives 128 a metre; the detail tile (8 cm at 128) gives 1,600.
  So: the tile carries what is seen from across a room, the detail carries the close look, and `tile` is
  chosen so the tile's largest feature fits four times or more.
- **Memory:** one surface at 256 is about 0.8 MB with its smaller copies and detail; a scene's sixteen, 13
  MB. At 512, 45 MB, which waits for the phone's compressed pictures (M62). `loop budget` counts it (M5).

## 9. What the phone does

Per pixel of a recipe surface:

| Read | When | Cost |
|---|---|---|
| colour and shape tiles | always; twice each for `box` where two faces meet (the two strongest only) | 2 to 4 reads |
| the wander picture | if the recipe has `vary.large` | 1 read |
| the detail tile | within `fade` | 1 to 2 reads |
| the two marks | always (they come with the vertex: 4 more bytes a vertex) | none |

Then steps 4 to 8 as plain arithmetic. The target: no slower than a model with its three pictures today,
measured on the recording page before M5 is accepted. The old patterns in the shaders stay until the last
flat material that uses one has retired; then they go, in that order.

## 10. Projection

- **`box`**: the tile laid on from the three sides and blended by the facing, weights sharpened (to the
  eighth power), only the two strongest read. For anything without a sensible unwrap: rock, plaster, most
  things made by shape words until S17.
- **`top`**: from above only. Land (with `box` taking over on cliffs, order W5).
- **`uv`**: along the mesh's own unwrap, one unit a metre. For cloth and anything whose grain must bend
  with the shape.
- **`space: "thing"`**: places are measured on the thing as it was made, so the surface moves, turns and
  scales with it. **`"world"`**: measured in the world; walls, floors and land pieces meet without a seam.
  Library `pieces`, `rooms` and `worlds` use `"world"`; everything else `"thing"`.
- **`along`**: a stretched pattern runs along the mesh's longest side unless told otherwise. A shape may
  say `along: "y"` to stand its boards up.

## 11. The library entry

```
# oak · pale brown oak, sawn and oiled: straight grain, open pores, a soft sheen
# For furniture, floors and beams. Wears paler on edges; gathers dark in joints.
#
# kind: surface
# place: test_ball("oak", AT)
# tags: wood, warm, indoor
# size: chair
# seeds: 1 to 4
# credit: J Production, made in Loop3D
# licence: our own work, free for every J Production project
# from: order M9 (date)

to oak(seed: 1)
  return surface("oak", seed: seed, tile: 2 m, …)
```

`loop library check` also refuses, for a surface: a non-metal base colour brighter than 0.9 or darker than
0.03; `metal` between 0 and 1; a pattern without a `size`; a `tile` that holds its largest feature fewer
than four times; more than one coat with a facing. Its picture (`loop library picture oak`) is the test
ball (order M8): a ball on a rounded plinth with a groove, a flat slab at a grazing angle and a 10 cm cube
for size, so base, wear, dirt, detail and repeat are all in one frame.

## 12. The seam in code

```cpp
// engine/render/surface.h  (order M3a makes it)
struct SurfacePattern { enum class Kind : uint8_t { kNone, kNoise, kSpeckle, kCells, kPores, kCracks, kVeins,
                          kStrata, kGrain, kPlanks, kBricks, kTiles, kWeave, kKnit, kBrushed, kChecker, kGrid };
  Kind kind = Kind::kNone; float size = 0.01f, stretch = 1, contrast = 1, sharp = 0.5f, piece = 0; int twill = 1; };
struct SurfaceLayer {            // base, a coat, wear or dirt: the settings each uses
  bool on = false;
  Vec3 color{0.5f}, color2{0.5f}; float roughness = 0.5f, roughness2 = 0.5f, metal = 0, bump = 0;
  SurfacePattern pattern;
  float cover = 0.5f, amount = 0, ragged = 0.5f, soft = 0.2f;
  enum class Sits : uint8_t { kAny, kHigh, kLow } sits = Sits::kAny;
  enum class Facing : uint8_t { kAny, kUp, kDown, kSide } facing = Facing::kAny;
};
struct SurfaceRecipe {
  std::string name; uint32_t seed = 1; float tile = 1;
  enum class Project : uint8_t { kBox, kTop, kUv } project = Project::kBox;
  bool worldSpace = false; int along = -1;       // -1: the longest side
  SurfaceLayer base, coat[2], wear, dirt;
  float varyLarge = 0, varyAmount = 0, varyRoughness = 0, varySmall = 0, varySmallAmount = 0; Vec3 varyTint{1};
  SurfacePattern detail; float detailBump = 0, detailRoughness = 0, detailTile = 0.1f, detailFade = 2;
  uint64_t fingerprint() const;                  // every number above
};
struct SurfacePoint { Vec3 color; float height, roughness, metal; };          // after step 3: what is baked
SurfacePoint surfaceTile(const SurfaceRecipe&, Vec2 u);                        // steps 1 to 3 (wraps at tile)
Surface surfaceAt(const SurfaceRecipe&, const Material&, Vec3 place, Vec3 facing, Vec2 uv,
                  float edge, float hollow, float distance);                   // steps 1 to 9
```

`Scene` gains `std::vector<SurfaceRecipe> surfaces`; `Material` gains `int surface = -1`. The cook gains
`Cook::marks(const Mesh&)` giving `struct Marks { std::vector<uint8_t> edge, hollow; }` (order M2). The baked
pictures are made and kept by `engine/render/surface_bake.*` (order M4), on the visual side; the marks are
facts about a mesh and stay with the cook in `engine/geometry/`. `engine/modules.json` gains one part,
"Surfaces" (visual): `engine/render/surface.*`, `engine/render/surface_bake.*`.

## 13. What it does not do

- **Patterns that run through a solid** (a marble block whose veins meet round its corners). The tile is
  flat; each face shows its own piece. Set aside until a hero object needs it.
- **Wear and dirt finer than the mesh.** They follow vertices. A thing with few vertices wears coarsely;
  the answer is the shape words' rounding, not a finer measure.
- **Dirt from other things** (the dark line where a wall meets the floor of another piece). Decals (M59)
  and the renderer's ambient occlusion cover it.
- **A picture per object.** No unwrapped, painted texture for one thing. If a star needs one, it is a new
  decision.
- **Wet, snow that settles, moss that grows over time.** A coat's `cover` can be changed by a script later;
  nothing here stops it, nothing here builds it.

## 14. Which order builds what

| Order | Builds |
|---|---|
| M2 | the marks (section 6) |
| M3a | `surface.h`, the word, the lookup of decision 11; the film shows the base layer |
| M3b | the patterns of section 5 in one place, the eight new ones among them, all wrapping |
| M3c | the whole stack in the film renderer (section 7) |
| M4 | the bake (section 8) |
| M5 | the phone reads it (section 9) |
| M6 | `box` and `top` projection, both renderers (section 10) |
| M7 | the detail layer (step 8) |
| M8 | the test ball and its picture (section 11) |
| M9 … M58 | fifty surfaces, one a card |

## 15. As built (2026-11-11, orders M3a to M5)

What building it taught, where this page was silent or wrong. Later orders follow this section.

- **The cluster drawing keeps the marks' borders.** The phone draws most meshes from the cook's cluster
  tree, whose simpler levels drop any point that costs no shape. A flat point beside a rounding costs none,
  so the rounding's wear spread across the whole face as bare steel (found on the phone, in
  `projects/samples/surface_box.loop`). A mesh drawn in a surface now gets its own tree
  (`Cook::clustersKeepingMarks`): every vertex where its marks change (`markBorders`) may only slide along
  its own band, and a slide counts the distance the band's border moves as its error, so that level is drawn
  only where the move looks small. Its test: `marks_simpler_levels_keep_the_wear_as_narrow`.
- **A lathe's outline goes from the bottom up,** or its faces look inward and the hollow mark's rays hit its
  own inside: the whole top gathers dirt. A flat stretch beside a groove or a rounding needs its own points
  (every 2 to 4 cm), or the band's mark spreads across the whole stretch (marks follow the mesh's points).
- **Where the phone does it:** the shader's stack is `surfaceHere` in `engine/gpu/shaders/scene.wgsl.h`
  (the model pictures it reads are bound there), called from `shade`, so the two ways of drawing (one
  draw a thing, and the clusters' visibility picture) share it. The GPU path tracer (`ultra.wgsl.h`, the
  Highest setting) shows a surface's base, flat, for now. Each surface takes two layers of the model
  pictures' stack (colour and shape, at `render.textureSize`), the wander picture one more; the material
  record carries steps 4 to 7's numbers (`surf0` … `coat2`); each vertex carries its marks as four bytes;
  each copy's own start in the tile is the film's hash of its number (`InstanceU::material[2]`).
- **No tint given** means the slow change only goes brighter and darker (the word sets the tint to the
  base's colour).
- **The test ball** is sofa-sized in triangles (13,752): a test rig, so its plinth is round.
- **Every pattern stretches** (order M3d): `stretch` is a feature's length along the tile's first way over its
  width, for every kind but `checker`, `grid` and `weave` (a pore becomes a dash, a tile a subway tile, noise a
  streak); under 1 it runs across (oak's rays). **`grain` is a sawn board's rings:** boards of about 48 rings
  side by side, each passing its log's rings at a depth that wanders slowly along it, so the rings arch where
  the face passes their tops and run straight and close towards the board's edges.
- **`loop library check` places a surface's example on the test ball** and counts no triangles for it (a
  surface has no shape of its own).
