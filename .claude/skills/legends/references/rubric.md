# The rank rubric

Score every forged or upgraded skill. Each dimension 0 to 10, weighted; total out
of 100. Record the score in the skill's `references/trials.md` and the registry.

| # | Dimension | Weight | 10 looks like |
|---|---|---|---|
| 1 | Mental model | 1.0 | A short model that classifies every problem in the domain by principle |
| 2 | Decision rules | 1.5 | 20+ situation → action rules, each with why and when-not |
| 3 | Failure knowledge | 1.5 | 10+ failure modes with symptom, cause, fix, early detector |
| 4 | Verification | 1.5 | A check for every kind of change, exact commands, clear pass criteria |
| 5 | Grounding | 1.5 | Names real files, conventions, commands and gaps in Loop3D |
| 6 | Education depth | 1.0 | Foundations → canon → frontier, with precise primary references |
| 7 | Numbers & taste | 1.0 | Calibrated numbers table and good-vs-bad worked examples |
| 8 | Trigger & clarity | 0.5 | Description fires on the right requests; tight, readable, no padding |
| 9 | Honesty | 0.5 | Frontier section; claims cited or marked unverified |

Total = Σ(score × weight). Max = 100.

## Ranks

| Rank | Score | Meaning |
|---|---|---|
| Common | < 40 | Facts and instructions. Not shipped. |
| Rare | 40-59 | Useful checklist. Not shipped. |
| Epic | 60-84 | Competent practitioner. Ship only as a draft, marked as such. |
| **Legendary** | 85-94 | Acts like a seasoned expert in this repo. The shipping bar. |
| **God** | 95-100 | Has passed trials on real tasks in this repo, caught a trap the naive approach missed, and has been revised from real use at least once. |

God rank cannot be reached on paper. It is earned in use: when a Legend's advice
proves right on real work (or is corrected by it), note it in `trials.md`; after
real-use revision, it may be promoted.

## Quick self-check (if any answer is no, it is not Legendary)
- Would a 30-year practitioner nod at the decision rules, or wince?
- Does it say what to do when the obvious approach is wrong here?
- Can Claude verify its own work in this domain from what the skill says?
- Does it know where in Loop3D the work happens?
- Is every line one that changes behaviour?
