# Lessons from 3D (incidents and traps, still true for LOOP3D)

## iPhone Safari and the web
- WebGPU on iOS 26 Safari; many browsers and older iPhones have none → the user
  asked for **WebGL2 for every browser** (3D's `looped/draw/` has both backends).
- **No threads**: IONOS can't send COOP/COEP, so no SharedArrayBuffer (Cloudflare
  Pages could). LOOP3D's TUNING already notes the service-worker trick.
- Sound starts only inside a tap; bless audio on the first touch. Never call
  `setTargetAtTime` every frame on Safari without cancelling (stutters).
- No Web MIDI on iPhone; no WebXR; motion sensors need a permission tap.
- `performance.now()` ≈ 1 ms resolution. Touch handlers: passive, queue only,
  never read layout inside; finger ids can be negative.
- Every JS↔WASM crossing costs on phones: batch through a mailbox per frame.
- `EM_ASM` bodies are minified: quote property names, `\.` and `\d` get eaten.
- Memory per tab is limited; long sessions heat the phone and slow it.
- 4K Mania (J117): taps froze the picture because of the phone's own gestures
  (3-finger undo bar, magnifier) and focused text fields; a gesture guard fixed
  it. Notes wobbled because the audio clock steps: run a smooth timer that eases
  toward the audio clock. 120 Hz is the phone's max.

## Measuring
- Headless browsers run at 1–2 fps and lose WebGPU: never judge speed there.
  Speed is judged only on the phone (`?perf=1` + Send report codes).
- The "GPU ms" from submit → done is queueing, not chip time.
- Ultra drew only ~30 new pictures a second while the clock ran 60 (reports
  N3VTFS/GADATU): watch pictures actually drawn, not ticks.

## Process incidents (root causes worth remembering)
- A rule written in Claude's paraphrase drifted from what the user said →
  keep the user's words verbatim and quote them.
- Verdicts written from a one-line summary without reading the spec → read the
  spec and the user's earlier words first.
- A rename searched only the engine, missed a build tool → search the whole repo.
- A new program shipped without opening it once → always open a new page/app once.
- New world things added without the seconds-long world test → run the check that
  covers what changed.
- A job number reused because another session's branch held it → check every
  branch before numbering.
- A plan promised tag pushes and branch deletions that this environment refuses →
  check the environment's limits before promising.
- The square of light: found by switching one feature off at a time on the same
  view; cause in bounce pacing never found.

## Hosting and CI
- IONOS uploads with `lftp mirror` without `--ignore-time` (same-size edits once
  skipped). GitHub Actions minutes ran out once (KI-44); the repo went public
  for free minutes. Deploys queue one at a time; a waiting deploy isn't hung.
- Models and music committed straight into git grow history fast (KI-41).
