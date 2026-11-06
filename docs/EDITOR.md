# The editor

One place to build worlds, make them play, film them and score them, made for the phone first (and just as
good with a mouse and keys). It lives in `web/editor.html` and `web/editor/`; it runs the same engine as the
player, so what you see while editing is what everyone sees.

## What people have hated for years, and our answer

Collected on 2026-11-06 from the places creators complain: Blender's "paper cuts" thread (2,600 replies,
[devtalk](https://devtalk.blender.org/t/blender-ui-paper-cuts/2596), [the developer task](https://developer.blender.org/T56950)),
Blender's crash and autosave reports ([single autosave lost](https://developer.blender.org/T88740),
[undo lost on quit](https://wiki.blender.jp/Doc:2.5/Manual/Vitals/Undo_and_Redo)), Unity's
[domain reload waits](https://discussions.unity.com/t/why-is-domain-reload-sooo-slow-and-will-this-ever-be-fixed/798176)
and [play-mode edits thrown away](https://discussions.unity.com/t/play-mode-data-is-different/725939), Unreal's
[editor frozen by shader compiles](https://forums.unrealengine.com/t/editor-ui-unresponsive-to-input-during-certain-shader-compile-situations/559022)
and [Blueprint spaghetti](https://forums.unrealengine.com/t/am-i-the-only-one-who-has-a-hard-time-with-blueprints-but-easier-time-with-c/2699997),
[Sequencer previews that differ from the render](https://forums.unrealengine.com/t/inconsistent-camera-panning-between-editor-viewport-camera-and-piloting-a-cine-camera-in-sequencer/2685631),
Godot's [empty editor while the game runs](https://lr.ggtyler.dev/r/godot/comments/1ewre2g/whats_one_feature_you_wish_godot_had/lj1sqgs/),
[Blender → Unity scale and axis trouble](https://www.katsbits.com/codex/unity-blender-fbx-scale), music apps'
[clutter and MIDI editors](https://www.kvraudio.com/forum/viewtopic.php?p=8124975), video editors'
[timelines that lag and crash as they grow](https://community.adobe.com/t5/premiere-pro-discussions/huge-performance-and-lag-issues-with-premiere-2022/m-p/13488616/highlight/true),
Roblox Studio's [forced redesign](https://piunikaweb.com/2026/01/06/roblox-studio-ui-forced-update-creators-furious/)
and no phone version, and what people loved: Nomad Sculpt
([made for touch first](https://gamefromscratch.com/?p=23875)) and Dreams (acclaimed for putting art, music,
animation and games in one place, but locked to one console and
[thin on teaching](https://www.digitaltrends.com/?p=3318382)).

| Years of complaints | The rule here |
|---|---|
| A wall to climb: quit in the first hour, hidden tool options, 187 shortcuts | **Everything is a named button you can see.** Shortcuts exist on a computer but are never the only way. Each tool says what it does in one line where you use it. |
| The same key does different things in different modes | **No hidden modes.** Four spaces (Build, Logic, Film, Sound) change which tools show, never what a gesture means. A tap always selects, a drag always moves, two fingers always move the view. |
| Panels everywhere, clutter | **The world fills the screen.** One sheet of tools at a time slides up from the bottom (phone) or sits at the side (computer). |
| Crashes and lost work, one autosave, undo gone after quitting | **Nothing is ever lost.** Every change is kept the moment it's made, as text, with a history of versions you can go back to; undo survives a reload. |
| Waiting: script reloads, shader compiles that freeze the editor | **Changes show at once.** A world rebuilds from its script in milliseconds; the editor never freezes while the engine works. |
| Changes made while playing are thrown away | **No throwaway play mode.** Press Play and keep editing; what you change stays. |
| The editor is empty while the game runs | **You always see the running world**, in the editor, as it really is. |
| Wires and nodes that turn into spaghetti | **Logic is short readable lines** (Looped Script), coloured, checked as you type, with the problem's line and its fix. No wires. |
| Wrong sizes and axes between tools | **One unit (the metre) and one up (Y) everywhere**; sizes are shown in metres. |
| The preview isn't what the render gives | **What you watch is the film:** the same camera and the same renderer as the final picture. |
| Timelines that lag and crash as they grow, cluttered music apps | **One light timeline** for shots, sounds and music, drawn by the page, not a heavy app. |
| A redesign forced on everyone | **Your layout is yours**: what you open stays open, on this phone. |
| Not on the phone at all | **Phone first**: big touch targets, thumbs at the bottom, gestures, safe areas. A computer gets the same editor with mouse and keys. |
| Locked to one console, lost when it ends | **Any browser, and the project is plain text you own** (`.looped` and `.loop` files in your repo). |

## How it works

- **Text is the source.** A world is its `.loop` script. Moving a thing in the 3D view rewrites that thing's
  line in the script (its `at:`, `turn:`, `scale:`, `size:`); adding a thing adds a line. The engine tells the
  editor which line made each thing, so the two never drift apart. A thing made by a repeat (`for …`) moves
  with its line and says so instead of guessing.
- **The edit camera.** One finger on empty space turns the view around what you're looking at, two fingers
  slide and pinch it, a double tap on a thing frames it. On a computer: right or middle drag turns, shift slides,
  the wheel zooms, F frames.
- **Picking.** A tap asks the engine what's under the finger (a ray through the scene, checked against each
  thing's real box), and the thing gets a soft outline and a label with its name and size.
- **Moving.** Drag a selected thing to slide it on the floor; the up handle lifts it, the ring turns it, a pinch
  sizes it. It snaps to 5 cm and 15° unless you turn snapping off.
- **Saving.** Each change goes into the scene's history (kept in the browser, newest 200 versions per scene),
  and the script text is what's kept, so it can be sent to the repo as is.

## The spaces

- **Build:** select, move, turn, size, add (box, ball, cylinder, cone, light, person, sound), duplicate, delete,
  its material and colour; the list of everything in the world.
- **Logic:** the scene's script, coloured, with line numbers; problems shown on their line with the fix; Run
  rebuilds the world at once.
- **Film:** the shots as blocks on a timeline; add a shot from where the view is now; scrub, play, and what you
  watch is the final film.
- **Sound:** the world's sounds (place them where they come from, hear them in 3D), and music on the same
  timeline.

## Built so far (2026-11-06)

- **The frame:** the world, the four spaces and their sheet (phone: at the bottom, its height dragged and kept;
  computer: spaces at the left, the sheet at the right), undo and redo, Play, the menu, Ember's colours, the old
  UI editor's line icons (51, drawn for J Production) and new ones.
- **The engine's hands:** `loop_edit_camera` (the editor camera, the world keeps running), `loop_pick` (what a
  point of the view meets, by the things' real triangles), `loop_view` (where things land on screen),
  `loop_thing` and `loop_thing_set` (a thing in full, and moving it at once); each placed thing knows its
  script line (`Stage::Placed::line`, tested in `tests/scene_api_test.cpp`); the work light (a lamp at the
  editor camera, never part of the world).
- **Build:** tap to pick (outline, name, size, line), drag to slide, handles to lift, turn and size (snapped),
  double tap frames; add a box, ball, cylinder, cone, wall, tree, person or lamp; copy (a `let` gets a new
  name), remove (refused while its name is used); where it is, its turn and size typed in; what it's made of
  (24 materials and 10 colours); the world's title and how it starts; the list of everything. Lamps and sounds
  are marks you pick and drag. A repeat's thing moves its whole line by an offset added to its expression.
- **Logic:** the script, coloured, with line numbers; Live rebuilds a moment after typing; problems on their
  lines; the language's words suggested as you type; the picked thing's line lit, and the caret's line picks
  its thing; Copy and Save .loop; the engine's messages.
- **Film:** shots on a timeline (tap to pick, drag to scrub, Watch); Shot from here, Retake, Ends here, Remove,
  length; the film's look (12 looks). The film's black bars show only here.
- **Sound:** listen; 15 sounds placed in the middle of the view, their volume; the room's size and echo.
- **Nothing lost:** the text kept at once (`localStorage`), undo and redo and 200 versions per scene
  (IndexedDB) through reloads; Menu → Versions.
- **Checks:** `tests/editor_source_test.js` (every script edit, and the engine reads them; in the quick checks)
  and `tools/editor_check.js` (the whole editor in a browser, phone and computer).

## Next

- **Music:** the engine's own instruments (from scratch) and notes on the same timeline as the film.
- **More hands:** pick lamps by their light, sun and sky by dragging, materials made and named, models from the
  project's folder, groups (a table and its chairs move together), people's walks drawn on the floor.
- **Logic as cards:** each line of a script shown as a card you can read and change with taps (still the same
  text underneath).
- **Saving to the repo from the phone** (needs you: a way for the editor to send to GitHub without a key in it).
