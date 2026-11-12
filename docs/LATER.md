# Later (parked ideas, in the user's words; nothing here is started)

Saved 2026-11-12 so no chat loses them. Each becomes small goals when the user says so.

## 1. Reports from inside the game
*The user:* "play the game, walk where I want, add an entry with the problem and the direction I'm looking, and
you can see what I'm looking at."
- A **Report** button in the player. One report saves: the user's words, a picture of the screen, where they
  stood and which way they looked, the scene, the quality level, how smooth it ran.
- Saved on the private recording page's database beside `runs` (the public player has nowhere private to save).
- "Check my reports": Claude reads them and puts the engine's camera on the same spot, facing the same way.

## 2. Study, never patch (the user: "an absolute necessity")
A problem is never fixed where it shows. For every report:
1. **Reproduce** it from the saved spot. No fix before that.
2. **Find what let it happen**, and where else the same mistake could hide.
3. **Change how that kind of thing is made**, so it cannot happen (not move the one wall).
4. **Lock it:** the report becomes a permanent test (that spot, that direction, the right picture).
5. **Write it down:** what it was, why, what now prevents it.
When this starts, the rule goes into `CLAUDE.md`.

## 3. The blueprint (the user's example of rule 2)
*The user:* a wall that overlaps is not nudged back; "when you create the seams you create a sort of model with
small lines that trace it", a blueprint that lets us add what we need or backtrack later.
- A room starts as **lines**: every seam (corner, wall to floor, a doorway's edge). Walls are built from the
  lines, so two walls sharing a line meet exactly on it.
- Later things (a door, a shelf, a light strip, a new room) snap to a line that exists.
- Move a line and what was built from it follows; every piece can say which line made it.
- A switch in the player draws the lines over the picture; a check refuses a scene where a piece is off its line.
- Same direction as the roadmap's sockets (`docs/ROADMAP.md`), one level up. First room: the Loft's shell.

## 4. The engine brain
- **Inside the engine:** an AI that only writes content (recipes, scenes, projects), checked by `loop check`,
  holding no keys. Never inside the frame, never engine code.
- **First test, nothing built:** the user gives one sentence; Claude writes the recipe from it alone and reports
  whether it passed first try, the tries, the tokens and the time.
- **Outside the engine:** the brain that upgrades the engine stays in chats, on a branch, behind the checks and
  the user's yes. A public page never carries a key to the repo.
- **Nightly, if wanted:** a scheduled chat builds one small order on a branch and leaves one line to approve.

## 5. The Tripo study and AI-written recipes
`docs/TRIPO_STUDY.md`.
