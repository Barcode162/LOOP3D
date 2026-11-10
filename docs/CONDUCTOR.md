# The conductor

How one chat runs many agents as a system: they all report to one board, the conductor checks it on a timer,
work that depends on other work waits and starts by itself, checks start the moment a wave is finished, and
every job's real time is kept so the next estimate is better. Written 2026-11-10 on the user's idea: agents that
"check in on each other and talk to each other … the check start exactly when everything is done … an occasional
check to see when things take certain amounts of time … always systematic and within expectation".

## The pieces
- **The board** is `work/claims/` on `main` plus each job's own branch, read by `python3 tools/claims.py --remote`.
  It is how agents in any chat talk: a claim says who holds which files; a check-in says how far a job has got;
  `waiting` with `depends:` says "I start when that is done"; `done` says "ship me"; `stuck` says "I need a look".
- **The conductor** is the chat that starts a wave (Sonnet or Opus; Fable for a wave of design). It starts the
  agents, watches the board, and is woken by each agent finishing and by a timer.
- **The agents** (`.claude/agents/`) check in on the board at each step.
- **The deployer** ships a finished wave; **the checker** runs the tests; **the judge** marks pictures.
- **The times** (`perf/jobs.md`) record how long each job really took; each size and hand's usual time becomes the
  next job's expected time (`tools/claims.py expect`).

## A wave, step by step (the conductor's playbook)
1. **Choose the wave:** jobs with full cards whose files don't clash (`docs/PARALLEL.md` lists the ready lanes).
   Name it (`w1`, `w2` …).
2. **Claim every job** on `main`, one command each, then one commit and push:
   `python3 tools/claims.py new E4 "A word's test in one line" --hand builder --size S --wave w1 --touch "tests/test.h, tests/words_test.cpp"`
   A job that must follow another gets `--depends E4` and is set to waiting:
   `python3 tools/claims.py beat S1 waiting "starts when E4 is deployed"`.
3. **Start one agent per job that isn't waiting**, all at once, each in the background, each in its own copy of the
   repo (the Agent tool with `isolation: "worktree"`), given its whole card and its code.
4. **Set the timer:** a check-in every 20 minutes while the wave runs (in a cloud chat: `send_later`, 20 minutes,
   "conductor check-in for wave w1"). The conductor is also woken whenever an agent finishes.
5. **At every wake-up**, in this order:
   - `python3 tools/claims.py --remote` (the board) and `python3 tools/claims.py --remote overdue`.
   - An agent that finished: read its report; if its claim isn't `done` or `stuck`, set it from the report.
   - **Overdue** (past 1.5 times its expected time, or silent for 30 minutes): ask that agent how it is (SendMessage
     to it); no answer or no progress by the next check-in: stop it and mark the job `stuck` with what's known.
   - `python3 tools/claims.py --remote ready`: start an agent for each waiting job whose dependencies are done.
   - `python3 tools/claims.py --remote wave w1`: exit 0 means every job is done, so **start the checks at once**:
     the deployer (which checks each job on the latest main, ships what passes, records its time). Exit 2 means
     something is stuck: ship the rest, re-cut the stuck job.
   - Nothing changed and nothing is late: set the next timer and say nothing to the user.
6. **When the wave is shipped:** stop the timer, tell the user in three lines (what shipped, what's stuck and why,
   what the times were against expected), and offer the next wave.

## Keeping it "within expectation"
- **Every job has an expected time before it starts,** from `perf/jobs.md` once there are three jobs of its size and
  hand, else a first guess (small 25 minutes, medium 60, large 120).
- **A job past 1.5 times its expectation is looked at, not left to run.** Most overruns mean the card was cut too
  big or needed a decision; the fix is to cut it again, not to wait.
- **The same checks every time:** the deployer runs the quick checks on every job, the picture tests on every job
  that could change a picture, and refuses any job that touched files off its claim.
- **After each wave,** the conductor looks at `perf/jobs.md`: a kind of job that keeps running long gets smaller
  cards next time; a hand that keeps coming back stuck gets its cards written by a stronger model.

## Across chats
A second chat (say, Fable writing the next cards) uses the same board: it claims its job with `tools/claims.py
new`, checks in with `beat`, and marks itself `done`. Any conductor's `--remote` board then shows it, a job that
`--depends` on it starts when it is done, and any chat's "deploy" ships it. Chats never message each other
directly; the board is the one place they meet, so nothing is lost when a chat closes.

## What the first big wave taught (loft1, 2026-11-10: 15 agents on a 4-processor machine)
- **At most four agents that build the engine at once.** Fifteen builds ran the machine out of memory: compilers
  were killed, jobs of 4 minutes took an hour, and the chat's own worker restarted and lost three agents.
- **Agents that only write a recipe must not build.** Build `loop` once on the latest `main` and give every
  asset-smith its path (`LOOP=/workspace/LOOP3D/build/loop`); then twelve at once is fine.
- **Each agent gets its own scratch folder** (`/tmp/<code>/`). One shared folder let agents overwrite each other.
- **Agents push early and often,** and leave no background commands running; an agent lost in a restart is
  started again from its branch.
- **A wave's time on the board includes the machine's lost hours:** correct `perf/jobs.md` when that happens.

## What it can't do
- It can't make a job faster than the agent doing it; it only notices sooner when one is off course.
- A timer check-in needs the conductor's chat to still be open (in a cloud chat it wakes itself; in the app,
  keep the chat). If the conductor's chat closes, the board keeps everything: a new chat says "conduct wave w1"
  and carries on from it.
