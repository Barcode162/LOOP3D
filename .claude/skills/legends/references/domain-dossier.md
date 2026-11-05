# The domain dossier: how to research a subject to mastery depth

Read this at stages 3 and 4 of forging. Goal: in one focused pass, gather what a
person would take a degree plus a career to collect, then keep only what changes
decisions.

## Pass 1: map (what exists)
1. List the field's sub-areas and how they depend on each other.
2. Find the 2-3 standard textbooks and the standard course syllabus (top university
   courses: their lecture topics are a good canon checklist).
3. Find the foundational papers and the most cited recent surveys.
4. Find the reference implementations (open source code people learn from).

## Pass 2: canon (what every expert knows)
For each core idea: one line on what it is, when it applies, when it does not,
and where it is best explained (exact chapter/section/page).

## Pass 3: the lifetime (what books leave out)
Sources, roughly from most to least valuable per hour:
- **Postmortems and bug trackers** of real projects: failure modes and fixes.
- **Conference talks by practitioners** (e.g. GDC, SIGGRAPH courses, CppCon,
  Handmade, PLDI): the tricks and the trade-offs, with numbers.
- **Practitioner blogs** by people who shipped: heuristics and war stories.
- **Reference code comments**: the "we tried X, it broke because Y" notes.
- **This repo's history**: `git log`, docs, perf notes in `perf/`, honest-status
  sections. The lessons already paid for here.

## Pass 4: distil
- Turn every fact into a judgment where possible: "X is true" → "so when Y, do Z."
- Cut anything a competent generalist already knows unless it is commonly
  forgotten or commonly wrong.
- Keep numbers. Keep failure modes. Keep trade-offs with a pick.
- Date anything time-sensitive (APIs, hardware, browser support).

## Pass 5: honesty
- Mark every claim you could not verify as `(unverified)`.
- Note where experts disagree, and what evidence would settle it.
- Prefer primary sources over summaries of summaries.

## Research tools
- Own knowledge first (fast), then WebSearch (`standard`, then `extended` for niche
  or recent facts), then the web-fetch agent to read specific pages.
- For codebase grounding: Grep/Glob over `engine/`, `docs/`, `tools/`, `tests/`.
