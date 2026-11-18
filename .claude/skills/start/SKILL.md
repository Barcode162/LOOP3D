---
name: start
description: Start a Loop3D chat the cheap way. Use at the start of every chat, when the user types /start (with or without a goal), or says "start", "let's begin", "where are we".
---
# Start a chat

1. `cd` to the repo, `git pull -q origin main`, `python3 tools/claims.py --remote`.
2. Read `docs/NORTH.md`, then `docs/START.md` sections 3 to 5. Read nothing else yet.
3. If the user gave a goal (the text after `/start`): find its row in START.md section 5, read only those
   lines (search, don't read whole files), then say the goal in one sentence and the one idea it teaches, and
   wait for "go".
4. If no goal: reply in at most five lines: where we are (START section 3), the next goal in one sentence,
   and "say go, or tell me what you want instead".
