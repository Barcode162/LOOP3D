# Core (engine/core/)

- `math.h` (367): Vec2/3/4, Quat, Mat3/Mat4, Transform, AABB, Ray, frustum, `kInf`.
- `random.h`: RNG (`Rng`, `uniform()`), hashing for seeds.
- `ease.h`: `Ease` curves shared by shots and scripts.
- `image.*`: float RGBA `Image`, sRGB, own zlib (LZ77 + fixed Huffman out, all block
  types in), CRC32, PNG encode/decode, PFM out.
- `json.*`: `Json` value, parse/dump (std::map objects: keys come out sorted).
- `jobs.*`: thread pool + `parallelFor`; inline on the web (no threads).
- `log.*`, `file.*`, `time.h`.
- `settings.*`: the registry (see [../architecture.md](../architecture.md#settings-enginecoresettings)).

**Tests:** core_test (9), settings_test (7).

**Gaps:** no memory allocators/arenas, no profiler markers in engine code (only
player-level frame recorder), no asset/resource system, no serialization of
scenes (editor can't save object edits back), no async file IO, no input
abstraction outside the player, no networking.
