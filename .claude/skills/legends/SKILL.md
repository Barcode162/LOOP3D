---
name: legends
description: The legendary skill-creation skill. Forges new skills that carry a full education in one subject (foundations to research frontier) plus a lifetime of practitioner tips, failure modes and decision rules, grounded in the Loop3D codebase. Use whenever a new skill is needed, an existing skill is too shallow, or the user asks to "make a skill", "master X", "forge a legend", or wants Claude to get expert-level at a subject (rendering, physics, compilers, audio DSP, animation, etc.). Also use to audit or upgrade any skill in .claude/skills/.
---

# Legends: the skill that makes masters

A normal skill tells Claude *what to do*. A **Legend** makes Claude act like the
person in the field who has done it for thirty years: knows the theory cold, knows
which part of the theory matters on Tuesday afternoon, has been burned by every
classic bug, and checks their work without being told.

Every skill forged here is a **Legend**, and its lineage is recorded in
`.claude/skills/README.md`.

## The core insight: what correlates with mastery

Mastery is not knowing more facts. Across every field studied (chess, medicine,
music, engineering, the expertise research of Ericsson, Dreyfus, Klein, Chi), the
same handful of things separates the expert from the knowledgeable novice. A skill
produces mastery exactly to the degree that it encodes them. Full detail and
evidence in [references/mastery-correlations.md](references/mastery-correlations.md).

| Masters have | So a Legend must contain |
|---|---|
| **Chunked mental models**: they see *structure*, novices see surface | A short "how it really works" model the rest of the skill hangs on |
| **Decision rules** under constraint: "if X, do Y, because Z" | Heuristics with the *why* and the *when not to* |
| **A catalogue of failure**: they recognise trouble before it lands | Failure modes: symptom, cause, fix, how to detect early |
| **Calibrated numbers**: orders of magnitude in their head | Numbers-to-know table (costs, limits, defaults, tolerances) |
| **Tight feedback loops**: they verify constantly and cheaply | A verification step for every kind of change, with commands |
| **Taste**: they know what "good" looks like | Worked examples, good vs bad, and the reason the good one wins |
| **Knowing the edge of their knowledge** | Honest "frontier / unsettled / ask the user" section |

The weak skill is a list of facts. The Legend is a list of **judgments**.

## Forging a Legend: the procedure

Follow all eight stages. Do not skip stage 6 or 7; they are what make it legendary
and not merely long.

### 1. Name the domain and its boundary
- One subject per Legend (e.g. `pathtracing`, `rigid-body-physics`, `bytecode-vm`).
  Too broad ("graphics") dilutes judgment; too narrow ("bvh-sah") belongs as a
  section of a bigger Legend.
- Write the one-sentence **mission**: what a master of this produces, in this repo.
- List **neighbouring Legends** it hands off to, so skills compose instead of overlap.

### 2. Build the mastery ladder
Write the Dreyfus ladder for this subject (novice → advanced beginner → competent →
proficient → expert → legend). For each rung: what they can do, what they still get
wrong. The Legend teaches *the gaps between rungs*. This tells you what content
earns its place: anything that only a novice needs and every competent person knows
is cut to one line.

### 3. The education: compress a full curriculum
Cover what a top university course sequence plus graduate study would, but only
the parts that change decisions. Use the dossier procedure in
[references/domain-dossier.md](references/domain-dossier.md):
- **Foundations**: the maths/physics/theory everything else derives from. State it,
  do not textbook it.
- **Core canon**: the 5 to 15 ideas, algorithms or papers every expert knows by name.
  One line each on what it is and when it applies.
- **Advanced and frontier**: the state of the art as of now, and what is still open.
- **Canonical references**: books, papers, talks, specs (with the specific
  chapter/section that matters). Prefer primary sources.
Research with WebSearch / web-fetch when your own knowledge is thin or might be
dated; record the date and source of anything time-sensitive.

### 4. The lifetime: harvest the tips and tricks
This is the part books leave out. For the subject, collect at least:
- **20+ heuristics** ("rules of thumb") with why and when-not.
- **10+ failure modes**: symptom → root cause → fix → early detector.
- **Numbers to know**: typical costs, ranges, defaults, budgets, tolerances.
- **Debugging playbook**: the order a master checks things in.
- **Smells**: signs in code or output that something is off before it breaks.
- **Trade-off tables**: option A vs B vs C, and which situation picks which.
- **Tricks**: the clever, non-obvious moves practitioners pass to each other.
Mine: practitioner blogs, postmortems, issue trackers, conference talks, code of
reference implementations, and the history of *this* repo (`git log`, docs).

### 5. Ground it in Loop3D
A Legend that does not know this codebase is a textbook. Start from
`CLAUDE.md`, `PLAN.md`, `docs/` and the code itself (the domain's folder under
`engine/`, its tests, `git log`) and link to them instead of repeating them. Add:
- **Where it lives**: the files, entry points, data structures (`engine/...`).
- **House rules**: the repo's conventions (from scratch C++20, no outside
  libraries, plain words in Looped Script, settings registered once in
  `engine/core/settings.*`, docs kept honest).
- **How to verify here**: exact build/test/check commands
  (`cmake --build build -j && ./build/loop_tests`, `./build/loop check ...`,
  `node tools/check_settings.js`, renders compared by eye, perf via `perf/`).
- **Known gaps**: what is missing in this domain in the engine today. Add them to
  `PLAN.md` → Later too.

### 6. Write it in the Legend shape
Use [references/forge-template.md](references/forge-template.md) exactly. Rules:
- `SKILL.md` stays under ~400 lines and holds the judgments: mission, mental model,
  decision rules, failure modes, verification, house rules. Depth goes in
  `references/*.md` (one topic per file, linked from SKILL.md with when-to-read).
- The `description` front-matter is the trigger: say what it does **and** list the
  words a user would say that should wake it. Be slightly pushy; under-triggering is
  the common failure.
- Write imperatives with reasons. "Do X, because Y" beats "ALWAYS X". A reason lets
  the model generalise to cases you did not foresee; a shout does not.
- Every claim is either general knowledge, cited, or marked as unverified.

### 7. Prove it: the trial
A Legend must earn its rank. Score it with
[references/rubric.md](references/rubric.md) and run the trial:
1. Write 3 to 5 realistic tasks from this repo in the domain (one easy, one hard, one
   trap where the naive approach is wrong).
2. For each, reason through how Claude would act **with** and **without** the skill.
   Where *with* is not clearly better, the skill is missing a judgment: add it.
3. Check the trap task specifically. If the skill would not have caught it, it is
   not legendary yet.
4. Record the score and the trial tasks in the skill's `references/trials.md`.
Rank required to ship: **Legendary** (rubric score ≥ 85). Below that, iterate.

### 8. Register the lineage
Add a row to `.claude/skills/README.md`: name, rank, mission, date, parent
(`legends`), neighbours. Commit with a message saying what mastery it adds.

## Upgrading an existing skill
Same rubric. Score it, find the lowest-scoring dimension, fix that first. The
cheapest big win is almost always stage 4 (failure modes and decision rules), the
second is stage 5 (grounding and verification commands).

## Tone of a Legend
Calm, precise, opinionated, honest. It tells you what the master would do and
why, admits what is uncertain, and never pads. If a section would not change what
Claude does, delete it.

## Self-application
`legends` is itself a Legend about the subject "making skills". When forging
reveals a better way to forge, update this file and
[references/mastery-correlations.md](references/mastery-correlations.md), and note
the lesson in `LOG.md`.
