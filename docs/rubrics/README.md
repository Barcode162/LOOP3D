# The rubrics

What the judge marks a picture against (`.claude/agents/judge.md`). Each file is one rubric: when to use it,
the reference described in words, and ten numbered questions. They come from `docs/CRAFT.md`, section 12; that
section stays the short list, these files are the working sheets. Every marking is kept in `perf/judge.md`.

| Rubric | Use it for |
|---|---|
| `base.md` | any picture; every library entry; every old scene |
| `hill.md` | Your Hill (milestone 1): the desert slope and its valley |
| `loft.md` | The Loft (milestones 3 and 4): the two-floor room at night or at sunset |
| `valley.md` | The Valley (milestone 6): river, towers, falls, terraces |
| `person.md` | one person, from full body to a close-up |
| `move.md` | a move, judged from a strip of frames |
| `shot.md` | one shot of a film, or a game camera's frame |
| `screen.md` | a phone screenshot with the game's screens on it |

## How a mark is given

Each question gets a whole number from 0 to 10 and one plain sentence of why, naming what is seen and where
in the picture. The same scale for every question:

| Mark | Means |
|---|---|
| 0 | not there, or broken |
| 2 | there, and plainly a computer's first try |
| 4 | right in kind, wrong in the particulars: you can say what is missing without looking hard |
| 6 | good until you look closely or put it beside the reference |
| 7 | would not look out of place beside the reference |
| 8 | holds up beside the reference at this size |
| 10 | you would take it for the reference |

Most early pictures deserve 2 to 4. A question the picture cannot answer (no near thing, no person) is marked
on what its absence costs the picture, never left out: the total is always out of 100.

**Broken comes first.** A picture that is black, has holes, missing things or the marks of flicker gets that
said before any mark, and question 10 is then 3 or less.

**The one change.** After the ten marks the judge names the single change that would raise the total most, as
something a builder can act on, and the question it lifts.

**A step is accepted** when the mark rose for the question it aimed at, no other question fell by more than
one, and the phone is no slower.

## Which questions an order aims at

An order's proof names the questions it must reach, by rubric and number ("base 2, 3, 4 and 6 at 7 or more").
A library model: base 2, 3, 4, 6. A surface on the test ball: base 2, 4, 6, 7, 10. Land: hill or valley 4, 5, 7.

## Adding a rubric

A new star or a new kind of picture gets its own file: ten questions, the reference in words, written by the
lead on the top tier. Questions 1 to 7 and 10 of a picture rubric stay the base's unless the file says why not,
so marks can be compared between stars.
