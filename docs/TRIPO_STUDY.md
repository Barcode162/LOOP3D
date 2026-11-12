# The Tripo study, and Loop3D's own AI (parked: a later day)

*The user's words, 2026-11-12:* get tripo3d.ai, make one generation with each feature, pull as much detail
out of them as possible, then do it ourselves; and AI will be in the program anyway, as long as it wastes no
tokens. Simple plan now, done on a later day, ready when the user says "start the Tripo study".

## What this can and cannot give us (say this before starting)
- A Tripo file shows **the result**, not **the method**. From a file we can measure everything about a good
  generated model: how many triangles, how the faces flow, how the surface is unwrapped, which texture maps and
  at what size, how parts are split, how the bones are laid. That becomes Loop3D's quality bar.
- The method is a very large neural network trained on millions of models on rented graphics machines. It cannot
  be read out of its outputs, cannot run in Safari on a phone, and training one breaks the **Free** rule. Much of
  the method is published anyway (Tripo's own open model TripoSR, and the research papers behind it); we read
  those rather than guess.
- So "do it ourselves" means: **our own way to the same quality**, not a copy of their network.

## Loop3D's own AI (already the roadmap's direction, `docs/ROADMAP.md` section on the AI-grown library)
A text model writes a **recipe** (`.loop`), and the engine builds the model from it. This is why it is cheap:
- a recipe is a short page of text (hundreds to a few thousand tokens), not millions of triangles;
- `loop check` and `loop library check` refuse a bad recipe before anyone looks at it;
- a made thing goes into `library/` once and is never generated again; a variation is a seed, which costs nothing.
Nothing built today is wasted by this: every word, piece and surface added to the engine is one more thing the
AI can use. It is not built into the program yet because the recipe words must be rich enough first (the Loft's
library is that work).

## The steps (each is one small goal; none started)
1. **Read Tripo's terms and prices** as they are that day: what the free plan allows, who owns the outputs, what
   they forbid (studying the service itself, training on outputs). The plan follows the terms. Paying is the
   user's choice (the **Free** rule); recommended: start on the free credits.
2. **The prompt sheet:** Claude writes one ready-to-paste prompt (or picture) per Tripo feature, using things the
   library already has as recipes, so the two can be put side by side: the leather armchair, the bar stool, the
   arc floor lamp, and one person for the bones.
   Features: text to model · picture to model · several views to model · texture and materials · cleaner or
   lighter mesh (retopology, low-poly, quads) · split into parts · bones and a move · style change.
3. **The user makes them** on the phone and sends each file (GLB) and a screenshot into the chat.
4. **The measuring tool:** a small build-side reader that prints a card per file (triangles, parts, maps and
   their sizes, bones, the unwrap's waste), and a picture of it in Loop3D's light next to our own recipe.
5. **The findings:** one page, `docs/TRIPO_FINDINGS.md`: what their models do better than ours, in numbers, and
   which of it becomes a rule in `docs/LIBRARY_PLAN.md`, a rubric mark, or a work order (a new recipe word).
6. **The first AI-made recipe:** one library entry written by a text model from one sentence, passed by the
   checks and the judge, with its token count written on its card. That number is the proof of "not wasted".

## Never, in this study
A Tripo model shipped inside a project unless its licence allows it and it is credited; a Tripo key or login in
the repo or the chat; their network, code or site copied; their outputs used to train anything.
