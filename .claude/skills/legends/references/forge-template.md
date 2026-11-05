# The Legend template

Copy this shape for every new skill at `.claude/skills/<name>/`. Delete any
section that would not change what Claude does; never pad.

```
.claude/skills/<name>/
├── SKILL.md                 # judgments (< ~400 lines)
└── references/
    ├── curriculum.md        # the full education, compressed
    ├── field-notes.md       # the lifetime of tips, tricks, numbers, war stories
    ├── failure-modes.md     # if the table outgrows SKILL.md
    ├── <topic>.md           # deep dives, one topic per file
    └── trials.md            # rubric score and trial tasks
```

---

## SKILL.md

```markdown
---
name: <kebab-name>
description: <What mastery this gives, in one sentence.> Use when <situations>, or
  when the user mentions <list of trigger words and phrases>. <Neighbour hand-off
  if needed: "For X use <other-skill> instead.">
---

# <Name>: <one-line promise>

**Mission:** <what a master of this produces in Loop3D>.
**Rank:** Legendary · forged <YYYY-MM-DD> by `legends` · neighbours: <a>, <b>

## The mental model
<5-15 lines. The deep structure of the domain: the categories every problem falls
into and how they relate. Everything below hangs on this.>

## Decision rules
<Situation → default action → why → the cue that means "not this time".>
| When | Do | Because | Unless |
|---|---|---|---|

## Failure modes
| Symptom | Root cause | Fix | Catch it early by |
|---|---|---|---|

## Numbers to know
| Quantity | Typical value | Note |
|---|---|---|

## Debugging playbook
1. <The first thing a master checks, and why it is first.>
2. ...

## In Loop3D
- **Where it lives:** <files and entry points>
- **House rules:** <conventions that apply>
- **Verify:** <exact commands and what pass looks like>
- **Gaps today:** <what is missing; mirrored in PLAN.md → Later>

## Taste: good vs bad
<2-3 short contrasts with the reason the good one wins.>

## Frontier and honesty
<What is unsettled, what to measure rather than assume, what to ask the user.>

## Go deeper
- [references/curriculum.md](references/curriculum.md): read when <...>
- [references/field-notes.md](references/field-notes.md): read when <...>
```

---

## references/curriculum.md

```markdown
# <Domain> curriculum
## Foundations      (theory everything derives from; stated, not taught)
## Core canon       (5-15 named ideas/algorithms/papers: what, when it applies)
## Advanced         (what proficient practitioners use daily)
## Frontier         (state of the art as of <date>, open problems)
## Canonical references (book/paper/spec + the exact section that matters)
## The mastery ladder   (novice → legend: can do / still gets wrong)
```

## references/field-notes.md

```markdown
# <Domain> field notes: a lifetime of tips
## Heuristics       (20+; each with why and when-not)
## Tricks           (non-obvious moves practitioners pass along)
## Smells           (signs something is off before it breaks)
## Trade-offs       (A vs B vs C tables, with the situation that picks each)
## War stories      (short postmortems: what happened, the lesson)
## Sources          (where each came from; mark unverified ones)
```
