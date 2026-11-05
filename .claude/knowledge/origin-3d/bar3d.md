# Bar3D and the live site (in Barcode162/3D)

## What's live
- **joshuavelasco.com**: J Production's company site (PHP on IONOS): Home
  (intro, one highlight, Enter the bar), Projects, News, Sign in, staff portal.
- **/bar/**: Bar3D, built on the first Looped (C++ → WASM → WebGPU). Main build
  is the old engine; the new engine is at the hidden link **/bar/?engine=next**
  (`/bar/labs/next/`, WebGPU or WebGL2). The switch (J119) waits for the user's
  "switch". Labs: /bar/labs/ (words, light, draw, physics).
- **/classic/**: the last public build from before the bar went private.
- **/dev/**: Dev, the control room (owner/admins): Overview with a live panel,
  Projects, Plan, Publish (news, highlights: the user's switches), People
  (accounts, Claude's key). Ember theme, nested navigation, animation.
- **/studio/** (parked), Dials, World notes, UI Editor (themes and screens), News.

## The bar's place (assets/world/places/bar.looped, 603 things)
Main hall with the L-shaped counter, stools, back bar (275 bottles), glasses,
tables, a stage, darts corner, the lounge (fireplace with shader flames,
Chesterfield, armchairs, mantel clock, painting, bookshelf with readable books),
the big mirror, string lights, neon, the jukebox, a grand piano, two racing rigs,
the arcade room (8 × 8 m, cosmic carpet, neon, 4K Mania cabinet, platformer
cabinet), room 3 (taped off), plus places `home`, `showroom`, `lab`.
63 lamps in 10 light channels (rig: assets/world/rigs/bar.looped).

## What you can do
Walk (left thumb), look (right), tap to use. Sit anywhere. Darts (301). Chess
(bots with Elo, full-screen with six calm themes). Piano (Salamander samples,
MIDI songs, plays itself, sparks from the strings, real MIDI keyboard on
computers). **4K Mania** (osu!mania-like; auto-charted songs plus a 30-map
Training pack, 2★ → 7★, TeknoAXE CC-BY songs, a sync calibrator). Racing sim
(240 Hz world, Pacejka tires, two players or bots) and Meridian (the user's
three.js driving game). The platformer "Test 2" (three.js). Bartending shifts
(badge). Jukebox (Kevin MacLeod CC-BY, same moment for everyone). Voice chat
(WebRTC, same age group only, signed in). Avatars (presets or a 17-number
custom look), emotes, mirrors (full or avatars-only). Credits with a 3D coin.

## Backbone
| Piece | Where | Notes |
|---|---|---|
| Site, bar files, stores, accounts | IONOS shared hosting (PHP 8.4, SFTP, no websockets, no COOP/COEP → no threads) | domain joshuavelasco.com; `private/` holds the stores |
| Room server + ledger | Cloudflare Workers + Durable Objects (free plan), `bar3d.joshuavelasco60.workers.dev` | one DO per room; authority for seats, claims, darts, glasses (runs the bar's C++ physics as WASM), jukebox, races; ledger in whole cents, idempotent rows, daily caps |
| Protocol | `docs/handbook/protocol.md`, `proto/vectors.json` | one WebSocket; binary STATE/PROP/TICK (20 Hz while moving); text messages; golden vectors both sides |
| Accounts | `server/account.php` | bcrypt, token hashes, birth date private (teen/adult only); `gate.php` is the door |
| Code + CI | GitHub `Barcode162/3D` (public since 10-25), Actions: `deploy.yml` (game + room, live in ~6 min, tests after), `hub.yml` (site), `plan.yml`, `classic.yml` | browser tests on request only; Emscripten 6.0.12 |
| Phone reports | Settings → Send report (or tap the build code 3×) → 6-letter code → `bar/report.php?code=XXXXXX` | Claude reads them; the real speed test |

## Known open problems (KI list, 2026-11-01)
Safety promised but not built on the server (KI-35); accounts never expire, no
guessing limit, no backups (KI-40); models and music in git history (76 MB+,
KI-41); browser tests on GitHub flaky in software rendering (KI-5); placement
mistakes and gaps across the bar (KI-45); game code one global state, darts rules
twice (KI-39).
