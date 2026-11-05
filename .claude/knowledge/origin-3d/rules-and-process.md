# Rules and process in 3D (the old Charter)

**Status: history** (the user retired this workflow on 2026-11-05: "another workflow from the past, I do things different now"). Was: **not confirmed.** These governed 3D. Some match how this
repo already works, some conflict with it (marked ⚠). Ask the user which carry
over before relying on them; until then follow this repo's CLAUDE.md and the
session's own instructions.

## The user's rules (newest first, condensed; word for word in 3D `docs/charter/rules.json`)
- Learn, then plan, then build (10-27).
- ⚠ One ship for everything, never idle: batch jobs, ship once from a worktree (10-27).
- ⚠ The user tests: Claude runs no tests before saving or shipping (only the
  seconds-long world check) (10-26). *This session ran the build and tests.*
- Research, then build: findings written into the job's plan first (10-25).
- Only what the user wants gets pushed: talk, questions, checks, try-outs never (10-24).
- Never assume the user knows anything; plain words (10-24).
- Zero errors per session: every error gets a root cause and a fix to the process (10-24).
- Nothing outside the guidelines: a verdict before any work (10-24).
- News highlights are for people; only the user publishes (10-24).
- The website keeps what works; it's J Production's company site (10-24).
- Dev holds everything except the main page and the games (10-24).
- The house theme is Ember (10-24). The bar's menus stay plain HTML (10-24).
- The site keeps few words (10-24).
- Light only comes from light sources: no fake fill; darkness is real; eyes adjust (10-22).
- Automation first: anything done twice becomes a tool (10-22).
- Wow and foundation take turns; every visual piece has an off switch and before/after pictures (10-22).
- "go" = one whole step; one piece = one commit with its own phone check (10-21).
- 13+ only; teens and adults apart; casino 18+ earned credits only (10-19).
- From scratch in C++: no outside engines or libraries (three.js only in Meridian and the platformer); every asset licensed and credited; no copyrighted music (10-15).

## The Never list (3D Charter §5)
No secrets in repo/commits/logs/replies · no unlicensed assets or copyrighted
music · no impersonation or fake records · never weaken the age rules · never
force-push or rewrite `main`; never delete `main`, `gh-pages`, `gallery`,
`classic` · never skip/disable/weaken a test to go green · ⚠ never open a pull
request · never work around a refusal or block (report it) · never publish news
or push a highlight · never say done/live without checking · never push a
conversation, question, check or try-out · never run checks that don't cover
what changed. **Anything not listed: ask first.**

## Ask first (3D)
Changing the rules; unrequested work or plan order; deleting outside the repo;
adding an outside library, font, model, sound or song; anything that costs
money; hosting, domains, secrets, GitHub/Cloudflare/IONOS settings; accounts,
ages, admins, credits; making something public; structural changes; a
multi-session experiment.

## The five books (how 3D was organised)
Charter (rules) > Playbook (the only instructions: a procedure per situation)
> Handbook (how everything works; generated pages) > Plan (`PLAN.md` from
`docs/plan/plan.json`: Now, road, Parked, Done; jobs J1…J122, pieces J<n>.<k>,
numbers never reused) > Records (requests word for word, incidents, sessions,
decisions, known issues) > Archive (history). Tools behind one front door
(`node tools/project.mjs …`), a preflight checklist, locks (`tools/lib/guard.mjs`
as a PreToolUse hook) so `main` moves only through the ship.

## Reply shapes the user is used to
- **Understood:** "Got it: <one line>. <verdict>. <what happens next>."
- **Done:** "Build `<sha>`. – **J<n>.<k>** <where to stand> → <what you see>. If
  not, say: broke J<n>.<k>." plus errors this session.
- **Question:** one decision, "I recommend **X** because …; other choice: Y".
- Verdicts: doable now · after X · at a cost (your call) · needs an experiment ·
  not possible (closest is …) · against your rule.
