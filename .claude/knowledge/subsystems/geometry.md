# Geometry (engine/geometry/)

**Files:** `mesh.*` (417: Mesh with positions/normals/uvs/tangents/indices;
`shapes::` box, roundedBox, sphere, icosphere, cylinder, cone, capsule, torus,
plane, lathe, tube, heightfield; transform, merge, normals, tangents, flatShade,
displace, weld, area, volume), `bvh.*` (binned SAH, flattened, closest and any hit,
Möller–Trumbore), `noise.*`, `terrain.*` (HeightMap generate with ridges and domain
warp, droplet hydraulic + thermal erosion, `toMesh(step)`, Bridson `poissonDisk`),
`plants.*` (`growTree` with conifer mode → bark + leaves meshes, `makeRock`,
`makeGrassTuft`).

**Tests (9):** closed volumes, outward normals, weld, BVH vs brute force, noise
ranges, erosion carves, Poisson spacing, plants grow, tube sides face out.

**Gaps:** no model import (glTF/OBJ: P0), no mesh simplification / LOD generation,
no CSG, no UV unwrapping, no BVH refit (rebuild only), no spatial streaming.
