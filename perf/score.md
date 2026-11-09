# The score against cinema

How far each live picture is from the cinema renderer's picture of the same view: the picture pipeline's
second number, beside milliseconds on the phone (`docs/PIPELINE.md`). `loop score tests/pictures` renders the
cinema picture of each picture test's view (the same camera and moment, widened for an upright screen as the
player does) and scores the live reference against it (`engine/render/score.h`): both halved, the live one
matched to the cinema's brightness, then the average colour difference a person sees (ΔE in CIELAB), film bars
left out. **0** is the same picture, **about 2** barely visible side by side, **10 and more** plainly
different. *Brightness* is live ÷ cinema before matching (each renderer meters its own exposure). Each phase
of the pipeline must leave every scene's score no worse (or better, when it means to change the look).

## 2026-11-09: phase 6 done (fog lit by the light cache)

```
scene                    score  worst tenth  brightness
showcase_hall             11.3         16.7        1.44
showcase_materials        24.4         36.4        1.41
showcase_light            28.6         76.6        0.52
showcase_sky               5.0         10.3        1.14
showcase_fog              12.5         23.9        1.28
showcase_physics          30.0         87.2        1.26
showcase_people           24.8         48.0        1.27
showcase_glade            24.9         45.5        1.12
courtyard                  9.0         17.3        2.23
cabin                     13.5         25.8        1.39
the_woods                 21.7         30.1        1.87
bar                       12.0         30.0        1.16
average 18.2 over 12 scenes (ΔE: 0 the same picture, about 2 barely visible, 10 plainly different)
```

## 2026-11-09: sunlit fog (god rays), with ambient occlusion and 2x upscaling

```
scene                    score  worst tenth  brightness
showcase_hall             11.3         16.7        1.44
showcase_materials        24.7         36.0        1.39
showcase_light            28.6         76.6        0.52
showcase_sky               5.0         10.3        1.14
showcase_fog              16.2         24.3        1.17
showcase_physics          30.0         87.2        1.26
showcase_people           24.8         48.0        1.27
showcase_glade            24.9         45.5        1.12
courtyard                  9.0         17.3        2.23
cabin                     13.5         25.8        1.39
the_woods                 21.7         30.1        1.87
bar                       12.0         30.0        1.16
average 18.5 over 12 scenes (ΔE: 0 the same picture, about 2 barely visible, 10 plainly different)
```

## 2026-11-09: phase 6, the light cache (bounced light from probes)

```
scene                    score  worst tenth  brightness
showcase_hall             11.2         15.9        1.44
showcase_materials        24.6         36.0        1.39
showcase_light            28.6         76.7        0.52
showcase_sky               5.0         10.1        1.14
showcase_fog              36.7         50.8        1.07
showcase_physics          30.0         87.3        1.26
showcase_people           24.8         47.9        1.28
showcase_glade            33.3         53.6        1.24
courtyard                  9.0         17.3        2.23
cabin                     13.5         25.7        1.38
the_woods                 21.0         30.0        1.96
bar                       11.9         29.6        1.15
average 20.8 over 12 scenes (ΔE: 0 the same picture, about 2 barely visible, 10 plainly different)
```

## 2026-11-08: the first scores (build 8dc4e62, cinema at 32 samples a pixel)

```
scene                    score  worst tenth  brightness
showcase_hall             12.3         17.8        1.47
showcase_materials        35.4         46.9        1.67
showcase_light            30.3         79.2        0.54
showcase_sky               4.9         10.1        1.14
showcase_fog              35.5         50.9        1.05
showcase_physics          39.2         93.8        2.21
showcase_people           34.8         73.6        2.67
showcase_glade            31.1         53.5        1.18
courtyard                  8.4         13.1        2.30
cabin                     16.4         27.9        1.59
the_woods                 20.8         29.9        1.96
bar                       11.9         29.6        1.15
average 23.4 over 12 scenes (ΔE: 0 the same picture, about 2 barely visible, 10 plainly different)
```

What the pictures show (live beside cinema, same view):
- **Closed rooms lit as if open to the sky.** Live ceilings and walls glow with sky light where no light
  reaches them in cinema (the light room's ceiling: bright live, black in cinema). The live renderer has no
  bounced light yet, only the sky's: the light cache, phase 6.
- **Rooms washed out and too bright.** The people and physics rooms are 2.2 to 2.7 times brighter live, their
  floors blown to white; the cinema's are lit by their lamps alone.
- **Surface patterns missing.** Wood floors, brick walls and tiles show in cinema and are plain live, on Phone
  quality (the patterns fade with distance there).
- **A haze outdoors.** The Glade, the fog room, the Woods and the courtyard sit in a bright haze live that the
  cinema picture doesn't have (it shows the sun's shafts instead).
- **In cinema, a ripple in the courtyard's bricks** (curved bands across the walls): a fault of the cinema
  picture itself, to look at.
