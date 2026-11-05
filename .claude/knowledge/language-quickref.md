# Looped Script quick reference

Complete list in one page. Full prose: `docs/LANGUAGE.md`; story craft:
`docs/STORY_GUIDE.md`. Authoritative word list: `./build/loop words json`.

## Syntax
- One statement per line; blocks by 2-space indent; `:` after a header optional
  (allows a one-line body). `#` comments.
- Units at read time: m cm mm km · s ms min h · deg degrees rad · `60%`=0.6 · K,
  lumens/lm, lux, nits, kg, g, Hz, kmh, mph · `lens: 50 mm`.
- Text `"… {expr} …"`, `{{` literal. `true/yes`, `false/no`, `nothing/nil`.
- `== != < <= > >= is, is not, and or not, in`; `+ - * / % ^`; vec maths; text `+`.
- `let x = …`; `= += -= *= /=`; `to f(a, b: default)`; `fn(x): x * 2`; closures;
  named args `f(who: "Mira")`; bracketless command calls `say greet "Mira"`.
  Multi-word arg names join with `_` (`look at:` = `look_at:`).
- `if / else if / otherwise / else`; `a if c else b`; `for i from 1 to 10 [by 2]`;
  `for each x in list|table|text|n`; `repeat 3 times`; `repeat forever`; `while`;
  `break`, `continue`.
- Concurrency: `every 0.5 s`, `after 3 s`, `when "event"` (`event` = payload),
  `emit "event"`, `task`, `wait 2 s`. Runaway loops stopped with their line.

## Standard library
say print · sqrt sin cos tan asin acos atan floor ceil round exp log sign abs min
max clamp lerp smoothstep degrees radians pi tau infinity · vec rgb hex kelvin grey
dot cross distance normalize length (.x .y .z .length .normalized) · ease(t, curve:
linear in out in_out smoother sine back elastic bounce spring) approach · random
random_int chance seed noise wave flicker · time text number type range keys list.
List: push pop sort reverse shuffle pick insert remove remove_at contains index_of
map filter find each sum join slice copy clear length first last, `l[-1]`.
Table: keys values has remove size. Text: upper lower split contains replace
starts_with ends_with trim repeat length.

## World words
| Area | Words |
|---|---|
| Piece | `title`, `mode "movie"\|"game"` |
| Materials | `material("oak")`, `material(like:, …props)`, `materials()`; 59 names; patterns checker tiles bricks wood planks marble noise ground |
| Models | model "x.glb" (at turn scale name material glow collide): from the scene folder or its models/ folder; pictures, metal, roughness, glow, glass |
| Shapes | box(size, round) sphere cylinder cone capsule ring ground rock tree(kind oak/pine/birch/bush) — all take at turn tilt rotation scale material/made_of name collide |
| Land | terrain(size height ridges seed erosion rain detail feature material), forest(kind around radius spacing clearing clear_around steepest seed leaves), height_at |
| Light | light(at lumens kelvin\|color radius aim angle softness aim_is_direction), panel_light, sun(elevation toward\|azimuth lux kelvin size), sky("physical" haze stars brightness ground \| "color" color nits), fog(density top glow color from to) |
| Camera/look | camera(at look_at lens f_stop focus shutter iso exposure auto_exposure sensor blades), look(name, contrast saturation warmth tint bloom bloom_size halation grain grain_size vignette bars tone lift gamma gain shadow_tint highlight_tint black_level fringes distortion sharpen monochrome) |
| Shots | shot(name length from:{} to:{} ease shake fade_in fade_out look), orbit(…) |
| Physics | solid(shape mass bounce friction velocity), push, gravity |
| People | person(at height skin shirt trousers facing lift); .walk_to .look_at .sitting .speed .position .facing .walking .visible .lift |
| Sound | sound(endless name at volume intensity \| one-off name variant at strength); .volume .position .playing; room(size echo) |
| Story | line(speaker text seconds), clear_lines, ask(q, options) → "choice", go_to, remember/recall/forget_story, player(), near(place within), move_player(at look_at), "use" event, `use "common"` |
| Renders | render(file quality width height samples at), film(folder quality width height samples fps) |
| Settings | setting "id", v · settings {…} · get_setting("id") |

Looks: natural blockbuster noir golden bleach seventies neon dream documentary
horror arctic western. Sensors: ff s35 apsc m43 imax.
