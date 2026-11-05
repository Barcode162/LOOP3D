# Geometry (engine/geometry/)

**Files:** `mesh.*` (417: Mesh with positions/normals/uvs/tangents/indices;
`shapes::` box, roundedBox, sphere, icosphere, cylinder, cone, capsule, torus,
plane, lathe, tube, heightfield; transform, merge, normals, tangents, flatShade,
displace, weld, area, volume), `bvh.*` (binned SAH, flattened, closest and any hit,
Möller–Trumbore), `noise.*`, `terrain.*` (HeightMap generate with ridges and domain
warp, droplet hydraulic + thermal erosion, `toMesh(step)`, Bridson `poissonDisk`),
`plants.*` (`growTree` with conifer mode → bark + leaves meshes, `makeRock`,
`makeGrassTuft`).

`gltf.*` (2026-11-05): `loadGlb` reads glTF 2.0 binary: node tree (matrix
or TRS) baked in, triangles merged into one mesh per material, POSITION/NORMAL/
TEXCOORD_0/TANGENT, u8/u16/u32 indices, metal-rough materials, emissive with
`KHR_materials_emissive_strength`, alpha modes, embedded JPEG/PNG pictures.
Not yet: skins, animations, morph targets, sparse accessors, external or base64
buffers, other extensions.

**Tests (9, + 2 in model_test):** closed volumes, outward normals, weld, BVH vs brute force, noise
ranges, erosion carves, Poisson spacing, plants grow, tube sides face out.

**Gaps:** glTF skins/animations and OBJ, no mesh simplification / LOD generation,
no CSG, no UV unwrapping, no BVH refit (rebuild only), no spatial streaming.
