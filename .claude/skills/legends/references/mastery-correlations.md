# Correlations between skill creation and mastery

Read this when forging or upgrading a Legend and deciding what content earns its
place. Each correlation is: what the expertise research finds, then what it means
for how a skill is written.

## 1. Experts perceive structure (chunking)
- **Finding:** Chase and Simon (chess, 1973): masters recall real positions far
  better than novices, random ones no better. Chi, Feltovich and Glaser (physics,
  1981): experts sort problems by underlying principle ("conservation of energy"),
  novices by surface ("has a pulley").
- **For skills:** open with a compact mental model naming the deep categories of
  the domain. Teach Claude to classify a problem by principle first, then act.
  Example for rendering: "every artefact is either bias, variance, or a bug in the
  measurement; find which before touching code."

## 2. Experts act on recognition, then check (recognition-primed decisions)
- **Finding:** Klein (firefighters, 1990s): experts rarely compare options; they
  recognise the situation, simulate the first workable action, and adjust.
- **For skills:** write *situation → action* rules, not option surveys. Give the
  default move and the cue that means "not this time."

## 3. Expertise is mostly knowledge of failure
- **Finding:** expert performance correlates with how many failure patterns are
  recognised early (medical diagnosis, aviation, software incident work).
- **For skills:** a failure-mode table (symptom, cause, fix, early detector) is the
  densest mastery per line you can write. Aim for the 10 to 30 that actually happen.

## 4. Deliberate practice needs immediate, specific feedback
- **Finding:** Ericsson (1993, 2016): improvement comes from practice with a goal,
  focus, and fast accurate feedback, not hours.
- **For skills:** every kind of change gets a verification step with an exact
  command and what "pass" looks like. The skill should make Claude *check itself*
  without being asked.

## 5. Experts hold calibrated quantities
- **Finding:** "Fermi" estimation and orders-of-magnitude intuition mark experts in
  engineering and science (and "latency numbers every programmer should know").
- **For skills:** a numbers-to-know table. Numbers let Claude reject wrong designs
  instantly ("that is 10x the frame budget") instead of after building them.

## 6. Tacit knowledge transfers through cases, not rules alone
- **Finding:** Polanyi ("we know more than we can tell"); apprenticeship and case
  method (law, medicine, business) exist because rules underdetermine action.
- **For skills:** add worked examples, good vs bad, with the reason. Two or three
  well-chosen contrasts teach taste that ten rules cannot.

## 7. Reasons generalise, commands do not
- **Finding:** in instruction-following, models (and people) apply a rule with its
  rationale to new cases correctly far more often than a bare rule.
- **For skills:** "Do X because Y" everywhere. Reserve absolutes for true
  invariants (data loss, safety, the repo's hard rules).

## 8. Experts know where their knowledge ends (metacognition)
- **Finding:** Dunning-Kruger and calibration studies: experts are better calibrated
  about what they do not know.
- **For skills:** a frontier section: open questions, contested choices, and things
  to ask the user or measure instead of assume.

## 9. Mastery is staged (Dreyfus model)
- **Finding:** novices need context-free rules; experts work from intuition and
  whole-situation judgment. Teaching above or below the learner's rung fails.
- **For skills:** Claude already has broad "competent" knowledge of most fields.
  The Legend should spend its words on proficient-to-expert content: priorities,
  trade-offs, exceptions, and the repo-specific reality. Cut textbook basics to a
  line.

## 10. Context beats generality
- **Finding:** expertise is domain and environment specific; transfer is weak.
- **For skills:** grounding in the actual codebase (files, conventions, commands,
  current gaps) is what turns general competence into effective action here.

## Anti-correlations (things that look like mastery but are not)
- **Length.** A long skill of facts Claude already knows lowers signal. Cut.
- **Shouting.** ALL CAPS MUSTs make the model rigid and brittle; explain instead.
- **Option surveys without a pick.** Masters recommend.
- **Unverified tips.** A wrong "trick" repeated confidently is worse than none.
  Cite or mark as unverified.
- **Overlap.** Two skills covering the same ground give conflicting advice. Name
  neighbours and hand off.

## The formula, in one line
**Mastery in a skill ≈ mental model × decision rules × failure knowledge ×
feedback loops × grounding**, and it is a product: a zero in any term makes the
whole skill weak, which is why the rubric scores each one.
