# The Studio: shapes, bones and movement, made in the editor

*Written 2026-11-11 on the user's word:* "the 3d blender like editor … full keyframes … edit shapes and
objects … animate with real skeletons, a full real smooth professional multi hundred bones body with perfect and
correct joints … all the brushes … edit right in the editor instead of using Blender … the backbone of our full
avatar models."

**The Studio** is the part of the editor where one thing is opened and worked on: its shape, its bones and its
movement. This page says where we stand, what "professional" means in numbers, the decisions everything else is
built on, the order of work, and who does which part. It is track **T** in `docs/ROADMAP.md`. It takes over the
first rungs of tracks P (people) and V (moves): P1 is T-B1, P3 is T-S1 to T-S3, V1 is T-K1.

It runs beside the Loft (the top priority in `PLAN.md`): other chats carry the Loft and the surfaces, this
track carries bodies and movement.

## 1. Where we stand against Blender (honest)

| What Blender does | What Loop3D has today | How far |
|---|---|---|
| **Place things in a world** (object mode) | Pick, move, turn, size, add, copy, remove, material, the list, undo that survives a reload. Missing: several things at once, groups in the view, snapping onto surfaces. | about 40% |
| **Edit a shape's points, edges and faces** (edit mode: extrude, inset, bevel, loop cut, knife, merge) | Nothing. A mesh is a bag of triangles (`engine/geometry/mesh.h`): no four-sided faces, no record of which edge meets which. | 0 |
| **Modifiers** (mirror, smooth, bevel, array, carve) | Shape words do a little of it in script (`lathe`, the rounded box, `group`). No stack you can see. | about 10% |
| **Sculpt** (brushes on a dense mesh) | Nothing. | 0 |
| **Paint** (bone weights, colours, masks) | Nothing. | 0 |
| **Bones** (an armature you build and edit) | 17 bones written in code (`humanSkeleton`): no hands, no face, no joint limits. They cannot be seen or changed in the editor. | about 5% |
| **Skin that bends with the bones** | Nothing. A person is sixteen stiff capsules; neither renderer can bend a mesh, and the model reader skips skins. | 0 |
| **Rig controls** (reaching, constraints, drivers, shape keys) | The maths for two-bone and chain reaching, and springs. No constraints, no shape keys. | about 10% |
| **Keyframes, timeline, dope sheet, curves** | The engine's `Clip` holds turn keys per bone, eased one fixed way, and nothing the user can touch uses it. The editor's timeline holds camera shots only. | about 5% |
| **Layers, pose library, ghosts, motion paths, recorded motion** | Two poses can be mixed. Nothing else. | 0 |

**In one line: for making bodies and animation we are about a twentieth of the way.** What is already strong is
what the Studio stands on: an editor made for a thumb with nothing ever lost, picking by real triangles, the
engine running live inside it, a timeline for shots, text as the source, two renderers that agree, reaching
maths, springs, a walker, physics with joints and cloth, and a build with threads and fast maths.

We do not copy Blender. Blender is thirty years of everything. We take the tenth of it that makes bodies and
movement beautiful and build that for a phone. Nomad Sculpt proved a phone can sculpt; nobody has yet made
rigging and keyframing good on one. That is the opening.

## 2. What "professional" means here (the bar, in numbers)

**The skeleton: about 330 bones, in three rings.** No animator moves 300 bones by hand; professionals move
about 70 and the rest follow by rule. The standard human:

| Ring | Bones | What they are | Who moves them |
|---|---|---|---|
| 1 · The ones you move | **67** | Root, hips, 4 spine, 2 neck, head; each arm: collar, upper arm, forearm, hand, and 19 in the hand (4 fingers of a palm bone and 3, a thumb of 3); each leg: thigh, shin, foot, ball; 2 eyes, jaw | You, by dragging; clips key these |
| 2 · The ones that follow | **about 110** | Twist bones along upper arm, forearm, thigh and shin (22); volume helpers at shoulder, elbow, wrist, hip, knee, ankle, knuckles (30); shoulder blades; every toe (28); ribs that breathe, belly, soft parts on springs; chains for hair, tails and loose cloth | Rules, from ring 1, every frame |
| 3 · The face | **about 150** | Lids, brows, cheeks, nose, a ring of about 40 round the lips, tongue, teeth, ears, throat, with about 50 shape keys for what bones cannot do | About 30 face sliders drive them |

**Joints that are right.** Each ring-1 bone has limits measured on real bodies (the published ranges of motion:
an elbow bends 0 to 150 degrees and never sideways; a knee 0 to 140), kept as *bend, side, twist*. And the
rules that make a body read as alive, not as a doll:
- raising the arm carries the collar and the shoulder blade (two parts arm to one part shoulder past the first
  30 degrees);
- turning the hand spreads along the forearm (the two forearm bones cross), never a candy-wrapper twist at the
  wrist;
- a bend of the back is shared along the spine; a look turns eyes, then head, then neck, then chest;
- the last two finger joints curl together; a fist closes little finger first.

**Skin that bends.** Each point of the mesh follows up to 4 bones live and 8 in film. Twist and helper bones
hold volume at shoulders, elbows, knees and hips; shape keys driven by the pose mend what is left (the crease of
an elbow, the bulge of a biceps). One way of skinning for both renderers (linear blend with helpers and
corrections); no second method.

**Keys.** Any number can be keyed: where a thing is, its turn, size, colour, a lamp's brightness, a lens, a
bone, a shape key, a surface's setting. Each key has its way in and out: held, straight, smooth, or free
handles; plus the named eases the engine has (`engine/core/ease.h`). Layers on top of one another, loops that
close, ghosts of the frames before and after, the path a hand travels drawn in the air.

**Brushes.** Draw, clay, inflate, smooth, grab, snake hook, crease, pinch, flatten, scrape, mask, relax, and the
same brush engine paints bone weights and colours. Mirrored left to right by default.

**The phone's budget** (iPhone 15 Pro Max, to be measured, not promised):

| | Target |
|---|---|
| One hero body, close | 330 bones, 40 to 60 thousand triangles, 4 bones a point |
| A body at 5 m / 15 m / a crowd | 180 / 67 / 25 bones: rings 3 then 2 fall away with distance |
| Working out all bones of one body, 10 layers | under 0.3 ms |
| Sculpting | 300 thousand points with the brush following the thumb; a million in film |
| Scrubbing the timeline | the pose under the thumb every frame |

## 3. The decisions everything is built on

These are the seams. They are decided once, by the lead, before any helper builds on them.

1. **Open a thing to work on it.** In Build, a picked thing gets **Open**. The Studio shows that one thing with
   three benches: **Shape** (points, sculpt, paint), **Bones** (the skeleton, the skin's weights, the rules),
   **Move** (poses, keys, curves). **Done** goes back to the world, which never stopped running. The editor's
   gestures hold: a tap picks, a drag acts on what is under the finger with the lit tool, a drag on empty space
   turns the view, two fingers always move the view. The four spaces stay four.
2. **Text is still the source.** Everything the Studio makes is written into `.loop` files as lines a person
   can read: bones with their names and limits, keys with their times, a shape's stack of steps. Only the bulk
   numbers (sculpted detail, weights of thousands of points, recorded motion) are packed: a readable header and
   a block of packed numbers under it, in the same text file. Nothing binary in the repo; git still shows what
   changed.
3. **Hand-shaped meshes join the shape words** (the user's word of 2026-11-11; it widens ROADMAP principle 1,
   "shapes are made by words"). Words stay the way for hard things (a chair, a stair). Hands in the Studio are
   the way for living things (a body, a face, a creature). A hand-shaped thing is still a recipe: a base, then
   steps (mirror, smooth, a sculpt layer), so it can be varied and never throws work away.
4. **One editable mesh.** A new structure beside `Mesh`: faces of any number of sides, every edge knowing its
   neighbours, and values carried on points and corners (unwrap, colour, bone weights, masks, shape keys), with
   names that survive an edit so undo, mirroring, weights and keys never break. The cook turns it into the
   triangles and clusters the renderers already draw. Smoothing is Catmull-Clark, with creases.
5. **One skeleton format, three rings** (section 2). A skeleton is a library entry
   (`library/skeletons/human.loop`): each bone's name, parent, rest, ring, limits and, for rings 2 and 3, the
   rule that drives it. Names match the common rigs so recorded motion and outside models fit. Animals and
   creatures are other entries of the same format.
6. **A turn is three numbers a person can read: bend, side, twist,** counted from the bone's rest. Limits are
   written in the same three numbers, curves are drawn in them, and the engine makes the rotation from them.
   The shoulder's rest sits mid-range so no human joint reaches the one place where three numbers fail (a half
   turn of swing). A free thing (a wheel, a box) keeps turn, tilt and roll, and may spin past a full turn.
   *Proved in T-K1 with a test over every joint's whole range.*
7. **A clip is channels of keys.** A channel names one value (`at`, `turn`, a bone's bend, a shape key, a
   lamp's brightness). A key is a moment, a value, and its way in and out. Keys sit on the frames of the clip's
   rate (30 a second unless said) and the engine can ask for any moment between. A clip is a library entry
   (`library/clips/`) made for a skeleton by name, so every body of that skeleton can play it.
8. **Changing a pose writes a key.** No separate "record" button to forget: on the Move bench, what you change
   at a moment is kept at that moment (undo takes it back). Nothing is lost, as everywhere in the editor.
9. **Posing is grabbing.** On a phone you do not turn 67 bones with rings. You grab a hand and pull, the body
   follows within its joint limits, and a tap pins what must stay (a foot on the floor, a hand on a rail).
   Turning one bone by hand is still there, second. This needs a whole-body solver with limits (T-P1).
10. **Skinned things are drawn by their own path.** Live: bones go to the graphics chip once a frame and bend
    the mesh there, with three or four whole-mesh levels by distance (not the cluster tree, whose boxes assume a
    still shape); shadows and frame blending get the same bones. In the live ray structures (reflections, the
    light cache) a body stays its bone capsules, as a person is today. Film bends the mesh on the processor
    before it builds its tree, so film is exact.
11. **One standard body, shared by every avatar.** One mesh, shaped once with care (loops round every joint,
    the mouth and the eyes), the same points in every person. Height, build, age and every face are shape keys
    on it. So weights, corrections, the face rig, clothes and clips are made once and fit everyone. This is the
    backbone the avatars stand on.
12. **Recorded motion and hand keys are equals.** A free-licence recording comes in as a clip of keys on the
    standard skeleton, and is then cleaned, layered and changed with the same tools as a clip made by hand
    (ROADMAP principle 6 stands; the Studio is where the cleaning happens).

## 4. The order of work

Eight stages, about 195 orders. Each stage is a run of small goals the user sees on the phone, one at a time.
The count gives a stage's size; nobody knows dates.

| Stage | What it is | Goals you see on the phone, in order | Orders | Needs |
|---|---|---|---|---|
| **K · Keys** | The timeline for anything | 1 a box slides across the room between two keys · 2 it eases: the curve under your thumb · 3 anything keys: a lamp dims, a colour turns · 4 the mannequin holds two poses and moves between them · 5 ghosts of the frames around · 6 a loop that closes, saved to the library | ~22 | — |
| **S · Skin** | A mesh that bends | 7 an arm of three bones bends with no gap, in film · 8 the same live on the phone · 9 a free test figure (a file with a skin) moves in both | ~14 | — |
| **B · Bones** | The 330-bone skeleton with right joints | 10 the skeleton seen in the editor, each joint stopping where yours does · 11 raise the arm: collar and blade follow · 12 turn the hand: the forearm twists along its length · 13 the old walk on the new skeleton | ~24 | S for 11, 12 |
| **P · Posing** | The rig's controls | 14 grab a hand and the body follows · 15 pin the feet · 16 a look that leads with the eyes · 17 mirror a pose; a shelf of poses | ~18 | K, B |
| **E · Shape** | Points, edges, faces | 18 open a box and pull a corner · 19 extrude, inset, bevel, loop cut · 20 mirror and smooth as steps in a stack · 21 a mug made by hand, in the library | ~30 | — |
| **C · Sculpt and paint** | Brushes | 22 the first brush on a ball of clay · 23 the fourteen brushes, mirrored · 24 levels of detail: big forms, then pores · 25 paint a bone's pull onto the skin · 26 paint where a surface's layer goes | ~28 | E; S for 25 |
| **A · The body** | The avatars' backbone | 27 the standard body, bending · 28 hands that close · 29 height, build and age as sliders · 30 a head and a face that moves · 31 a second person who is not the first | ~30 | S, B, E, C |
| **M · Movement, professional** | Everything a studio animator has | 32 layers: wave while walking · 33 the whole curve editor · 34 a hand's path drawn in the air · 35 a recording brought in, fitted, cleaned · 36 hair and cloth that follow through · 37 the first sixty moves | ~30 | K, P |

**Keys come first** because that is the user's first ask, and because a key on a box needs nothing else: the
idea is seen on day one. **K, S, E can run at the same time** (different files). B follows S's first half; P
needs K and B; C needs E; the body (A) is where everything meets, so it is late on purpose: a body made before
its tools exist would be made twice.

**What this track must not collide with.** The surface work holds the live renderer's files today
(`engine/gpu/renderer_scene.cpp`, `renderer_internal.h`, the scene shaders: claim M7). Live skinning (T-S3)
needs those files, so it starts after that wave has shipped, and film skinning (T-S2) goes first. Every order
is claimed on the board before it starts (`docs/PARALLEL.md`).

## 5. Wave 1: the foundation (the lead's, one at a time)

Eight orders. They set every format later orders build on, so they are not for helpers.
`QUICK` is the quick checks of `CLAUDE.md`.

```
T-K1 · The clip: channels, keys, curves, as text                    hand: lead      size: M
Read:   engine/anim/anim.h (Clip, Pose), engine/core/ease.h, docs/STUDIO.md section 3 (6, 7), docs/LANGUAGE.md
Touch:  engine/anim/clip.h, engine/anim/clip.cpp (new), tests/clip_test.cpp (new), CMakeLists.txt,
        docs/STUDIO.md (section 6: the format, written down as built)
Do:     1. Channel (a named value of 1, 3 or 4 numbers), Key (frame, value, way in, way out: hold | line |
           smooth | free handles | a named ease), Clip (name, rate, length, loop, channels).
        2. Sample any moment: smooth keys pass through without overshoot unless handles ask for it; a loop's
           last key meets its first with no jolt.
        3. A turn as bend, side, twist from rest (decision 6), both ways, for bones; turn, tilt, roll for
           free things, unwrapped past a full turn.
        4. Reader and writer for the text form; what is written reads back the same to the last digit kept.
        5. The old anim::Clip keeps working (the walker does not use it; its one test stays green).
Proof:  QUICK; tests: read-write-read is the same; a smooth curve through 5 keys has no overshoot; bend, side,
        twist to a rotation and back over every joint's whole range to 0.01 degree; a loop's join has the same
        speed on both sides.
Not:    any editor work; any change to the walker.
```
```
T-K2 · Keys in a scene: the words, and the stage plays them         hand: lead      size: M
Needs:  T-K1
Read:   engine/script/scene_api.h (Stage, Placed, the one clock), engine/script/words_camera.cpp (shot)
Touch:  engine/script/words_anim.cpp (new), engine/script/scene_api.*, engine/script/words_internal.h,
        tests/words_test.cpp, docs/LANGUAGE.md (its lines), projects/samples/keys.loop (new)
Do:     1. A way to write, on a thing's own lines, keys for its at, turn, scale and colour; and a lamp's
           brightness and colour. Refused by name with the nearest right word when wrong, as `material` does.
        2. The stage plays every clip on the one clock (play, hold, loop, back and forth); film and live agree.
        3. The moment can be set from outside (for scrubbing) without the world's own time moving.
Proof:  QUICK; `./build/loop render projects/samples/keys.loop` at three moments shows the box at the three
        places a test worked out by hand; `loop check` names a bad key's line.
```
```
T-K3 · The engine's hands for keys                                  hand: lead      size: S
Needs:  T-K2
Read:   web/player.cpp (loop_thing, loop_thing_set, loop_scene_json), docs/EDITOR.md ("The engine's hands")
Touch:  web/player.cpp, web/editor/engine.js
Do:     loop_clip (a thing's clip in full), loop_clip_set (keys changed at once, no rebuild), loop_moment (set
        or read the moment; play, pause). A changed key shows in the same frame.
Proof:  QUICK; the web player builds (tools/build_site.sh); tools/editor_check.js still passes.
```
```
T-K4 · The Move bench: a timeline with keys                         hand: lead      size: M
Needs:  T-K3
Read:   web/editor/spaces.js (the Film space's timeline), web/editor/source.js, web/editor/history.js
Touch:  web/editor/studio.js (new), web/editor/source.js, web/editor/spaces.js (the Open button),
        web/editor/editor.css, web/editor/icons.js, tests/editor_source_test.js, tools/editor_check.js,
        docs/EDITOR.md
Do:     1. Open on a picked thing; Done. The Studio's frame with three benches (Shape and Bones say what is
           coming, in one line each).
        2. Move: a strip of time with a row per keyed value and a diamond per key; drag to scrub, Play, tap a
           diamond to pick it, drag it to another moment, remove it.
        3. Moving, turning or sizing the thing at a moment writes that key (decision 8) into its script lines;
           undo takes it back; a reload loses nothing.
Proof:  QUICK; editor_check.js on a phone-sized and a computer-sized page: open a box, set two keys, scrub
        to the middle, the box is half way; the script holds both keys; undo removes the second.
See:    goal 1 on the phone: a box slides across the room between two keys.
```
```
T-K5 · The curve under your thumb                                   hand: lead      size: M
Needs:  T-K4
Touch:  web/editor/studio.js, web/editor/editor.css, web/editor/source.js, tests/editor_source_test.js
Do:     A picked key shows its way in and out as four big buttons (held, straight, smooth, free) and the named
        eases; one value's curve drawn large, its handles sized for a thumb, the view pinched and slid with two
        fingers. What the curve shows is what film renders.
Proof:  QUICK; editor_check.js: a key set to "held" keeps the box still until the next key.
See:    goal 2: the same slide, now starting gently and landing softly.
```
```
T-B1 · The standard skeleton, ring 1: 67 bones, names, limits       hand: lead      size: M
Read:   engine/anim/anim.cpp (humanSkeleton), docs/STUDIO.md sections 2 and 3 (5, 6)
Touch:  library/skeletons/human.loop (new), engine/anim/skeleton.h, engine/anim/skeleton.cpp (new),
        engine/project/library.* (the kind `skeletons`), tests/skeleton_test.cpp (new), CMakeLists.txt
Do:     1. The format: name, parent, rest, ring, limits in bend, side, twist, mirror partner; read and checked
           (a name used twice, a parent that comes later, a limit the wrong way round: refused by name).
        2. Ring 1 written out, proportions from measured bodies, limits from the published ranges of motion,
           each with its source in the card.
        3. A pose held to its limits (the nearest allowed turn, never a snap).
Proof:  QUICK; tests: 67 bones, every left bone has its right; the rest pose stands 1.75 m; every limit holds
        when a pose is pushed past it; `loop library check` passes the entry with its credit and licence.
Not:    rings 2 and 3 (T-B2, T-B3); any change to the walker (T-B6 maps it).
```
```
T-S1 · Points that follow bones, and film that bends them           hand: lead      size: M
Needs:  T-B1
Read:   engine/geometry/mesh.h, engine/render/scene.h, engine/render/scene_bvh.*, engine/script/words_people.cpp
Touch:  engine/geometry/mesh.*, engine/anim/skin.h, engine/anim/skin.cpp (new), engine/render/scene.*,
        engine/render/scene_bvh.*, tests/skin_test.cpp (new), projects/samples/arm.loop (new)
Do:     1. A mesh may carry, per point, up to 8 bones and their pulls (summing to one).
        2. Bend a mesh by a pose on the processor, on every core; normals bent with it.
        3. Weights worked out for a mesh from its bones (smooth, never crossing a gap: an arm's bone does not
           pull the ribs beside it).
        4. Film bends each skinned thing before building its tree.
Proof:  QUICK; tests: a rest pose leaves every point where it was; no point moves farther than its bones
        do; a tube bent 90 degrees has its volume measured and kept as the figure the helper bones (T-B2) must
        beat; the film picture of projects/samples/arm.loop bent at three angles.
See:    goal 7: an arm of three bones bends with no gap.
```
```
T-E1 · The editable mesh                                            hand: lead      size: M
Read:   engine/geometry/mesh.h, engine/geometry/cook.*, engine/geometry/marks.*, docs/STUDIO.md section 3 (2, 4)
Touch:  engine/geometry/edit_mesh.h, engine/geometry/edit_mesh.cpp (new), tests/edit_mesh_test.cpp (new),
        CMakeLists.txt, docs/STUDIO.md (section 6)
Do:     1. Faces of any number of sides, half-edges, values on points and corners, names that survive edits.
        2. From a Mesh (triangles joined back into quads where they were quads) and to a Mesh (for the cook).
        3. The four moves every tool is made of: split an edge, split a face, join two points, remove a face;
           each leaves the mesh whole (checked after every move in tests).
        4. Smoothing (Catmull-Clark with creases) to a level.
        5. The text form: a readable header, packed numbers under it; read back the same.
Proof:  QUICK; tests: a cube smoothed once has 26 points and 24 four-sided faces at the places worked out by
        hand from the rule; a creased edge stays sharp; ten thousand random moves leave a whole mesh; a round
        trip through text is the same mesh.
Not:    any tool in the editor (stage E's orders, for helpers, each one tool on these four moves).
```

After wave 1 the helpers have what they need. The next cards are cut then (one tool, one brush, one rule or
one reader a card), because their exact shape depends on what wave 1 proves.

## 6. The formats, as built

*Empty until T-K1, T-B1 and T-E1 write them down here.*

## 7. Who does what

| Work | Hand | Tier (`docs/MODELS.md`) | Share |
|---|---|---|---|
| This plan; wave 1; every later seam (the whole-body solver T-P1, weights from bones, the brush engine's core, levels of sculpted detail, the standard body's layout, live skinning in the renderer) | the lead | Top | about a quarter |
| Merging a wave, mending what crosses parts, the phone's timings | the lead or a strong chat | Strong | as needed |
| One tool, one brush, one rule, one reader, one bench's panel, one clip, each from a complete card | builder, asset-smith | Workhorse | about three quarters |
| Running the checks | checker | Quick | every wave |
| Marking a pose, a bend or a move against its rubric (`docs/rubrics/`: person, move; new: bend, sculpt) | judge | Workhorse, reviewed by the lead | every goal seen |

**The rule:** helpers never start before the seam they build on is on `main`. A helper sent early invents its
own format, and the work is done twice. After wave 1, waves of four to eight helpers at once are right
(stages K, E and C split cleanly into one-file orders).

## 8. Choices that are the user's (asked when reached, not now)

- **Where the standard body comes from** (stage A): grown and shaped entirely here, in our own Studio (slower,
  wholly ours), or begun from a public-domain base mesh whose licence is checked first (faster). Recommended:
  ours, with a free test figure used only to prove that skin bends (goal 9).
- **Which recordings of human motion** (stage M): only sets whose licence allows use in games that are sold,
  each credited in its clip's card.
