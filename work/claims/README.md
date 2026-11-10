# Claims: who is working on what

One file per job in progress, named by its order code (`E4.md`, `L-cholla.md`). The lead writes it on `main`
before starting an agent, so every other agent and session can see it. The agent works on its own branch,
`work/<code>`, and changes only its claim's status there. The deployer deletes the claim once the work is on
`main`. `python3 tools/claims.py` lists them and refuses two claims that touch the same file.

```
code:   E4
title:  A word's test in one line
hand:   builder
branch: work/E4
status: working        # working | done | stuck | deployed
touch:  tests/test.h, tests/words_test.cpp
pictures: no           # yes if the picture could change: the deployer then runs the 12 picture tests
note:   (the agent writes its report's first line here when done or stuck)
```
