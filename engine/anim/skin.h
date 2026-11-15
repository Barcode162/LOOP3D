#pragma once
// Skin: a mesh whose points follow bones (docs/STUDIO.md, "Skin that bends").
// Each point is pulled by up to 8 bones, the pulls summing to one. A pose
// gives every bone a matrix (where it is now, undone by where it rested), and
// a point goes to the pull-weighted mix of where each of its bones would
// carry it: linear blend skinning, the one way for film and live.
//
// The pulls are worked out from the bones themselves. A bone pulls a point it
// can see (a straight line from bone to point that crosses no surface: an
// arm's bone never claims the ribs beside it) by how far it is, and reaches
// the points it cannot see only along the surface. The nearest bone pulls
// most; the others fade with how much farther they are, over a width that
// follows how thick the limb is there. So a joint bends smoothly, both its
// bones pulling alike at its middle, and a gap is never crossed.
#include <vector>

#include "engine/anim/anim.h"
#include "engine/geometry/mesh.h"

namespace loop::anim {

struct WeighOptions {
  // How wide a joint's blend is, as a share of the limb's thickness there:
  // smaller is a sharper crease, larger a rounder bend.
  float soft = 0.5f;
};

// Works out `mesh.pulls` from a skeleton standing in its rest pose.
// `boneWorld` is where each bone rests (Pose::rest(s).world(s, root)), and
// `meshToWorld` places the mesh in that same space. Bones without length
// (and those no point is nearest to) pull nothing.
void weigh(Mesh& mesh, const Mat4& meshToWorld, const Skeleton& s, const std::vector<Transform>& boneWorld, const WeighOptions& o = {});

// One matrix a bone, in the mesh's own space: where the bone is now, undone
// by where it rested. A rest pose gives every bone the matrix that changes nothing.
std::vector<Mat4> bendMatrices(const Mat4& meshToWorld, const std::vector<Transform>& restWorld, const std::vector<Transform>& nowWorld);

// Bends `rest` (a mesh with pulls) by the bones' matrices into `out`: points,
// normals and tangents, on every core. A mesh without pulls comes out as it went in.
void bend(const Mesh& rest, const std::vector<Mat4>& bones, Mesh& out);

}  // namespace loop::anim
