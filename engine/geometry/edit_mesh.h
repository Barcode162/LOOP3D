#pragma once
// The editable mesh (docs/STUDIO.md, order T-E1): the shape a hand reshapes.
// A Mesh is triangles for drawing; an EditMesh is faces of any number of
// sides that know their neighbours, with values on points (place, weights,
// mask) and on corners (unwrap, colour), and names that never change, so undo,
// mirroring, weights and keys keep finding the same point or face.
//
// How it is held: each face is a loop of corners; a point or face that is
// removed stays in its place (alive = false) until compact(), so numbers stay
// good while a tool works. halfEdges() builds the neighbour view on demand.
// Whole (check() says ""): every face has 3 or more corners on live points,
// no face uses a point twice, every edge has at most two faces and they run
// it in opposite ways, every live point is used, names are unique, every
// crease lies on an edge. The four moves below leave a mesh whole or refuse.
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/geometry/mesh.h"

namespace loop {

struct EditMesh {
  struct Point {
    Vec3 pos;
    uint32_t name = 0;  // never reused, never changes
    Pulls pulls;        // bone weights (all zero when the shape is not a skin)
    float mask = 0;     // 0 to 1, for sculpt masks
    bool alive = true;
  };
  struct Corner {  // one face's view of a point
    uint32_t point = 0;
    Vec2 uv;
    Vec4 color{1, 1, 1, 1};
  };
  struct Face {
    uint32_t name = 0;
    std::vector<Corner> corners;  // in order round the face (the same way a Mesh's triangles wind)
    bool alive = true;
  };

  std::vector<Point> points;
  std::vector<Face> faces;
  // Edges that stay sharp when smoothed: key (low point, high point) -> sharpness. 1 or more holds the
  // edge sharp for that many levels of smoothing; between 0 and 1 softens it. Edges at a boundary are always sharp.
  std::map<std::pair<uint32_t, uint32_t>, float> creases;
  uint32_t nextName = 1;

  // --- counting and finding (names are looked up by search: fine for a tool, not for an inner loop)
  size_t pointCount() const;
  size_t faceCount() const;
  int pointByName(uint32_t name) const;  // -1 when none
  int faceByName(uint32_t name) const;
  int findEdge(uint32_t a, uint32_t b) const;  // some face that has the edge a-b either way, or -1
  float creaseOf(uint32_t a, uint32_t b) const;
  void setCrease(uint32_t a, uint32_t b, float sharpness);  // 0 removes it; refused (no-op) if there is no such edge

  // --- building
  uint32_t addPoint(Vec3 pos);                                     // returns its number
  int addFace(const std::vector<uint32_t>& pointsInOrder);         // returns its number, -1 if it would not be whole
  void compact();                                                  // drop removed points and faces; renumbers
  void tidy();                                                     // remove points no face uses and creases on no edge
  std::string check() const;                                       // "" when whole, else the first thing wrong

  // --- the neighbour view: half-edge number = start[face] + corner
  struct HalfEdge {
    int face, corner;
    uint32_t from, to;
    int twin, next, prev;  // twin is -1 on a boundary
  };
  struct HalfEdges {
    std::vector<HalfEdge> he;
    std::vector<int> start;  // per face, -1 for a removed face
  };
  HalfEdges halfEdges() const;

  // --- the four moves (each returns false / -1 and changes nothing when the result would not be whole)
  // Puts a new point on edge a-b, `t` of the way from a (0 to 1); every face on the edge gains a corner.
  int splitEdge(uint32_t a, uint32_t b, float t = 0.5f);
  // Draws an edge across a face between its corners i and j; returns the new face's number (the old keeps its
  // name; the new one gets a fresh name), or -1.
  int splitFace(uint32_t face, uint32_t i, uint32_t j);
  // Joins point b into a: a stays (and its name), moves to the middle, and the faces round b now meet it.
  bool joinPoints(uint32_t a, uint32_t b);
  // Takes a face away (points left unused go with it).
  bool removeFace(uint32_t face);

  // --- smoothing: Catmull-Clark with creases, `levels` times. Every old point keeps its name; new points and
  // faces get fresh ones. Corner values (unwrap, colour) are carried across by averaging along the face.
  EditMesh smoothed(int levels = 1) const;

  // --- to and from a Mesh
  // Points are joined where positions agree; triangles that were a quad (a,b,c then a,c,d on one plane) join
  // back into one four-sided face. Each vertex's unwrap goes on its corner.
  static EditMesh fromMesh(const Mesh& m);
  // For the cook: each face cut into triangles, a vertex for each (point, unwrap) pair, smooth normals.
  Mesh toMesh() const;

  // --- the text form: a readable header, then one line of numbers per point, face and crease (floats as the
  // 8 hex digits of their bits, so they come back exactly). Always written compact.
  std::string toText() const;
  static bool fromText(const std::string& text, EditMesh& out, std::string* error = nullptr);

  bool operator==(const EditMesh& o) const;
};

}  // namespace loop
