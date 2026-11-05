# Audio (engine/audio/)

**Files:** `audio.h` (78), `audio.cpp` (575). 48 kHz, all synthesised (no samples).

- `Voice::render(out, n)`; `makeSound(name, variant, strength, seed)` one-shots:
  footstep (wood stone grass gravel snow water carpet metal), impact (wood metal
  glass stone cloth plastic), bell, click, thud, whoosh (modal synthesis).
- `makeAmbience(name, intensity, seed)`: fire wind rain stream birds crickets hum
  night applause crowd strings chains synth drones heartbeat.
- `surfaceOf(materialName)` maps materials to sound families.
- `Mixer`: 3D placement (pan, interaural delay, distance, air absorption, optional
  Doppler), reverb (`roomSize`, `roomWet`), limiter, per-category volumes from
  `config().audio`. Web: the page pulls frames via `loop_audio` on the main thread.
- `writeWav` for offline checks.

**Tests (4):** every sound plays and ends, materials sound like themselves,
left/right and distance, never clips harshly.

**Gaps:** mixing on the main thread (NB `audio.worklet`, a hitch source), no
occlusion/obstruction, no music system or sample playback (no imported audio),
no HRTF, no voice/dialogue audio.
