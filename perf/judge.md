# The judge's sheet

Every marking the judge has given, newest first: one line per picture. The judge (`.claude/agents/judge.md`)
ends each marking with a line in this form; the lead pastes it here, then adds whether the step was accepted.
The rubrics are in `docs/rubrics/`; the scale (what a 2, a 4, a 7 means) is in `docs/rubrics/README.md`.

The ten marks are in the order of the rubric's questions, so a column means the same thing down the sheet for
one rubric. A step is accepted when the mark rose for the question it aimed at, no other question fell by more
than one, and the phone is no slower.

| Date | Picture | Rubric | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | Total | The one change | Aimed at | Accepted |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|

*No markings yet: the first goes here when the first picture is judged.*

## How to read it

- **Picture:** the file judged, and the commit it was rendered from (`build/pictures/hill_01.png @ 1a2b3c4`).
  A picture that is kept goes in `perf/judged/` under the same name.
- **Aimed at:** the question the step was meant to lift ("hill 8").
- **Accepted:** yes or no by the rule above, with the reason when no.
- **The one change** of an accepted step becomes the next order, or is set aside here with a reason.
- A second marking of the same picture after a change is a new line; the old one stays.

## What the sheet is for

- The lead reads the last line for a star before cutting its next order.
- A question that stays low over five markings of one star means the orders are aiming at the wrong thing:
  the lead rethinks that rung.
- Every tenth marking the lead (on the top tier) re-marks one picture without looking at the judge's line. If
  the two totals differ by more than 10, the rubric's wording is tightened in `docs/rubrics/`.
