# The Light Lab sheet's rows: every experiment's measured numbers (perf/lab/<scene>/*.jsonl, written by
# tools/light_lab.js) joined with its words below. Run: python3 perf/lab/rows.py  ->  perf/lab/results.json
import glob, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
# (scene, name): (date, experiment, what changed, lamp work, what we learned)
WORDS = {
 ('loft', 'today'): ('2026-11-19', "Today's light", "Nothing: the Loft as the phone draws it now (Phone quality)", 31,
   "16 away from film, and 1.8 times too bright. The ceiling and brick glow evenly orange where the film's fall into dark."),
 ('loft', 'no_bounce'): ('2026-11-19', "Bounced light off", "The light cache (probes of bounced light) switched off", 31,
   "Changed the picture by 0.05 out of 255: in the Loft, live has almost no bounced light at all."),
 ('loft', 'no_ao'): ('2026-11-19', "Corner darkening off", "Ambient occlusion switched off", 31, "Almost no change (1 out of 255). Not where the error is."),
 ('loft', 'no_lamp_shadows'): ('2026-11-19', "Lamp shadows off", "Shadow maps for the main lamps switched off", 31,
   "Almost no change (0.8 out of 255). Only 2 of 46 lamps have shadows on Phone, so turning them off barely matters."),
 ('loft', 'all_lamp_shadows'): ('2026-11-19', "Every lamp shadowed (switch)", "The High-only switch for every lamp's shadow, on Phone", 31,
   "No change: that switch only works on High."),
 ('loft', 'high'): ('2026-11-19', "High quality", "Ray-traced shadows for every lamp, screen reflections (High)", None,
   "14.4 and 1.4x: shadows for all lamps close part of the gap."),
 ('loft', 'ultra'): ('2026-11-19', "Ultra: rays every frame", "Live path tracing at a few rays a pixel", None,
   "13.1 and 1.5x, grainy. Even the film's own method, live, keeps a gap."),
 ('loft', 'every_lamp_alone'): ('2026-11-19', "No lamp grouping", "Each lamp worked out alone instead of far groups merged", 31,
   "No change: lamp grouping isn't the cause."),
 ('loft_city_only', 'today'): ('2026-11-19', "Only the city light", "Every lamp inside at 0 lumens; only the city's glow beyond the glass", None,
   "6 away, but 4x too bright: the city lamp has no shadow live, so it lights the table through the roof."),
 ('loft_inside_only', 'today'): ('2026-11-19', "Only the lamps inside", "The city's glow removed; every lamp inside on", None,
   "15.5 away: the lamps inside carry almost all the gap."),
 ('loft_direct_only', 'today'): ('2026-11-19', "Film with no bounce", "The film picture made with direct light only; the phone unchanged", 31,
   "16.0 and 2x too bright, the same as against the full film: the phone isn't mainly missing bounce, it adds direct light the film blocks."),
 ('loft_strips', 'today'): ('2026-11-19', "Only the strip lights", "Stair strips, coves, shelf strips on; other lamps at 0", None, "15 times too bright: the cause. A strip's lamps cast no shadows on the phone, so light hidden in its channel, cove or shelf pours across the whole room."),
 ('loft_pendants', 'today'): ('2026-11-19', "Only the pendants", "Pendant domes and globes on; other lamps at 0", None, "Right brightness (1.2x), 11 away: light lands in the wrong places (no shadows), but the amount is right."),
 ('loft', 'bake'): ('2026-11-19', "Round 3: the light bake", "Still surfaces read light the film renderer baked once (every lamp with its shadow, the bounce, the sky) in 10 cm cells; lamps add only their shine", 31,
   "10.9 away and 1.2x: the best yet (today 16.1, High 14.4, Ultra 13.1). The ceiling falls dark and the table casts its shadow, as in the film. 122,525 cells, about 6 minutes to bake on the lab's computer."),
 ('loft_spots', 'today'): ('2026-11-19', "Only the spotlights", "The track spots on; other lamps at 0", None, "2.2x too bright: spots light past what should block them too."),
}

rows = []
for f in sorted(glob.glob(os.path.join(HERE, '*', '*.jsonl'))):
    for line in open(f):
        r = json.loads(line)
        key = (r['scene'], r['name'].rstrip('2'))
        if key not in WORDS or any(x['id'] == f"{r['scene']}-{key[1]}" for x in rows): continue
        date, exp, change, lw, finding = WORDS[key]
        rows.append(dict(id=f"{r['scene']}-{key[1]}", date=date, scene=r['scene'], experiment=exp, change=change,
                         switches=r['params'], score=r['score'], worst=r['worst'], brightness=r['brightness'],
                         lamp_work=lw, finding=finding))
json.dump(rows, open(os.path.join(HERE, 'results.json'), 'w'), indent=1)
print(len(rows), 'rows')
