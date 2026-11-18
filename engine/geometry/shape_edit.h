#pragma once
// A shape opened and reshaped by hand (docs/STUDIO.md, order T-E2). The recipe (`box(...)`) is kept; each
// hand edit is a step written under it (`pull point: 3, by: vec(0.1, 0.2, 0)`, `extrude face: 2, by: 0.3`). The recipe's Mesh is opened
// as an EditMesh (engine/geometry/edit_mesh.h), whose points have names that never change, so the same
// recipe always gives the same names. Steps apply in order.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/geometry/edit_mesh.h"

namespace loop::shapeedit {

struct Step {
  uint32_t point = 0;  // a pull: the point's name
  Vec3 by;             // a pull: how far it is pulled, in the shape's own space (before the thing's place and size)
  enum Kind { Pull, Extrude } kind = Pull;
  uint32_t face = 0;   // an extrude: the face's name
  float out = 0;       // an extrude: how far the face goes out along the way it faces (in, when below 0)
};

// A shape in hand: the recipe's points, the steps so far and the points as they now are.
struct Shape {
  EditMesh base;  // the recipe, opened
  EditMesh now;   // the recipe with every step applied
  std::vector<Step> steps;
};

// Distances in a step are kept to a tenth of a millimetre, so the text says exactly what the shape is.
Vec3 tidy(Vec3 v);
float tidy(float x);
// The step as the script says it: `pull point: 3, by: vec(0.1, 0.2, 0)` or `extrude face: 2, by: 0.3`.
std::string stepLine(const Step& s);
// Pulls one point of `m`. Returns "" when done, else the reason (it names the point and what the shape has).
std::string pull(EditMesh& m, uint32_t point, Vec3 by);
// Extrudes one face of `m`: the face goes `out` along the way it faces, and a wall of four-sided faces joins
// it to where it was, one on each of its edges. Its points stay as they are; the face moves to new points
// (named after every point there is), the walls get new names too. "" or the reason (it names the face).
std::string extrude(EditMesh& m, uint32_t face, float out);
// Where a face's middle is and the way it faces (unit length), in the shape's own space; false if there is none.
bool faceFrame(const EditMesh& m, uint32_t face, Vec3& middle, Vec3& facing);
// Opens a recipe's Mesh.
Shape open(const Mesh& recipe);
// Does a step and keeps it; "" or the reason.
std::string apply(Shape& s, const Step& step);
// The shape as it is drawn: flat faces, as the box it came from.
Mesh drawn(const EditMesh& m);

// The engine remembers the shapes it built from recipes with steps, by the mesh that draws them (the owner is
// the stage), so the Studio can open one. A record is only good while the mesh still is what it drew.
void remember(const void* owner, int mesh, const Shape& s);
Shape* recall(const void* owner, int mesh);

}  // namespace loop::shapeedit
