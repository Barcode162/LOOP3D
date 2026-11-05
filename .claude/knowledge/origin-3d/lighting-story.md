# The lighting story (what was tried, what the user said)

The single hardest, most repeated topic. Read before any light or look work.

| When | What was built | What the user said / found |
|---|---|---|
| 10-15 → 10-22 | "Classic": forward lights, glow, mood | first shader "looked amazing, but had some problems and eventually completely made it dark" |
| 10-23 L1 | Eyes (metering, AgX, `eyeKeep`), lumens/Kelvin, real albedo, bounced light (patches + probes), lights with size | squares of light on the floor fixed (L1.8); matte coats (M0) |
| 10-25 #10 | bloom removed (J92) | "glow on every light … like I'm being blinded … get rid of the artificial glow and let only realistic light" |
| 10-25 #17 | | "a weird reflection circle … following me", "small squares on the ground", "patches of light … with no lights making them" |
| 10-26 #1–#3 | lamp shadows (J97), less gloss on tables, floor a bit shinier, ⅓ stop darker | "the brightness is … sort of blinding", "squares of light … all over the bar" |
| 10-26 #12 | string lights, mirror bulbs, spots give real light (J74) | "the lights seem to be existing just to give light"; fog beautiful but less |
| 10-27 #4 | J113: lights only on glowing things, thinner haze, whiter strings | "too warm … like a foggy room … extremely bright lights instead of the objects giving out light … square patches … we've never found the right fix" |
| 10-27 #5 | strings ~5000 K industrial white | |
| 10-27 #7–#9 | J116 one formula (baked per channel, relit by script), settle and repair | user loved "already calculated freeing up lighting needs … only then does that part get recalculated" |
| 11-02 #5 | | baked new lighting "feels fake at completely"; ray tracing "too computationally expensive"; wants realism, cheap, **inside and outside alike**, judged in many zones; "decode light into code so I can replicate it" → J122 design |

## Rules distilled
1. Light only from visible sources; every glowing thing gives light, nothing
   invisible gives light. No bloom/glare/starbursts.
2. No square/blotchy artefacts from probes, grids or pacing: the root cause of the
   10-25 square (bounce at its own pace, J88.3) was never found; suspect any grid,
   cache or temporal reuse first.
3. Not too warm by default; haze subtle; overall not blinding; dark places stay
   dark but never pitch black (eyes adapt, film curve).
4. Real rooms have more fill light than direct-only renderers show: the reference
   path tracer is the truth; measure against it.
5. Static baked light reads fake: it needs view-dependent response (specular,
   reflections) and light on moving things.
6. The same light model must work inside and outside, at any time of day.
7. Everything has an off switch and before/after pictures; the phone is the judge.

## LOOP3D today vs these rules
LOOP3D's live renderer has bloom on by default (`Look::bloom = 0.04`,
`look` presets add more) and halation. That contradicts rule 1 as the user
stated it for the bar. Ask before changing defaults; at least keep bloom
opt-in for their projects.
