# Which model for which work

For the user, who switches the model in the chat (`/model`) and starts helper agents. It exists so that when
the subscription grows, the strongest model is spent only where it changes the result, and everything else
runs cheaper. Written 2026-11-10. Model names will change; the *tiers* are what matter.

| Tier | Today's name | Use it for | Cost |
|---|---|---|---|
| **Top** | Fable 5.2 | Design that sets a format or a seam; judging whether a picture is good; the hard maths; reviews; the plan | highest |
| **Strong** | Opus 5.5 | Most engine work: a shader change, a new system inside agreed seams, merging agents' work, debugging | high |
| **Workhorse** | Sonnet 5.5 | Carrying out a written order: a word with a test, a reader, a library entry, a preset, docs that follow code | medium |
| **Quick** | Haiku 5.5 | Running checks and reporting, renaming, formatting, listing files | lowest |

The four helper agents already name their tier in `.claude/agents/`: builder and asset-smith and judge run on
the Workhorse tier, the checker on the Quick tier. The lead is whatever the chat is set to.

## The rule I follow

**When the work in front of me needs a different tier than the chat is on, I say so before starting**, in one
short block, and then wait:

> **Switch suggested:** `/model claude-fable-5-2` (Top). Why: this sets the shape of the surface-recipe format;
> everything after it depends on it. Expected: one sitting. After it, switch back to `claude-sonnet-5-5`.

I also say when the chat is on a *higher* tier than the work needs ("this is mechanical; Sonnet is enough").
I never switch myself; you do. I never run a Top-tier task on a lower tier silently: I say what I would be
risking.

## What needs which tier

**Top (spend it rarely, on these):**
- Anything that decides a format, a word's meaning or a seam others build on: surface recipes (M1), shape word
  design (S1 to S4 as a set), the screen layout solver (U3), the clip and skeleton formats (P1, V1).
- Algorithms with real depth: the land's carving (W1, W2), motion matching (V5), skin weights (P3), cutting
  rules and the director (C5, C7), the cook's bake (M4, M5).
- Judging: is this picture good, which change helps most, is this plan sound; reviewing the judge's marks.
- Writing a new milestone's orders. Quarterly rethinks of `docs/ROADMAP.md`.

**Strong:**
- Splitting the big files (E2, E3), shader changes that alter the picture (A2, A3, A7, M60).
- Debugging anything that crosses parts; performance work on the phone's recordings.
- Reviewing and merging a batch of Workhorse work.

**Workhorse (most orders):**
- Every order marked "builder" or "asset-smith" in `docs/WORK_ORDERS.md` whose card is complete.
- Library entries, surface cards, presets, kit samples, tests, docs, themes.

**Quick:**
- Running `QUICK`, `pictures`, `site`, and reporting. Moving files, renaming, listing.

## How to deploy helpers (what to tell the chat)

You say what you want in any words; I turn it into orders. If you would rather start them yourself:

- *"Run builder on E4"*: I start the builder with that card. Several can run side by side if their "Touch"
  lists do not overlap; I will say which.
- *"Make the next ten library entries"*: I start ten asset-smiths in order of `docs/LIBRARY_PLAN.md`, each in
  its own copy of the repo, then the judge on each, then I merge what passes.
- *"Judge this"*: the judge marks a picture and names the one change.
- *"Check it"*: the checker runs the quick checks.

A batch of library entries is the best use of a bigger subscription: they are independent, each is small, and
the judge catches the weak ones.

## Waves: how the work is shaped for cost

1. **Top tier writes the order cards** (design decided, files named, proof stated): once per rung.
2. **Workhorse carries them out**, many at once.
3. **Quick checks them.**
4. **Strong merges, fixes the odd failure, and records the result.**
5. **Top judges the picture** and decides the next rung.

If a Workhorse agent reports "stuck", "needs a design decision" or touches files off its list, that card goes
back up a tier: it was cut wrong, not done badly.

## When to move up a tier mid-task

- Two attempts at the same fix failed.
- The task turned out to set a format after all.
- A picture keeps scoring the same after three rounds of changes.

## When the subscription grows

Order of value for extra budget: (1) more Workhorse agents in parallel on the library; (2) Top-tier time on the
hard designs listed above, one per sitting, with the pages they write kept in `docs/`; (3) a nightly picture
run and judge pass; (4) Top-tier review of each finished milestone before the next begins.
