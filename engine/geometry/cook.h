#pragma once
// The cook: what the engine makes from a recipe once and keeps, by the
// recipe's fingerprint. Text is the source (a script's shapes, a model); the
// cook turns each mesh into what drawing needs, the first time it's asked,
// and hands the same result to every copy and every later ask. Today it
// makes cluster trees (cluster.h); collision shapes, pictures and what each
// thing offers (stand on, hold, sit) join it in later steps of the rebuild.
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "engine/geometry/cluster.h"
#include "engine/geometry/mesh.h"

namespace loop {

class Cook {
 public:
  static Cook& shared();
  // Every number of the mesh that shapes it (positions and triangles).
  static uint64_t fingerprint(const Mesh& m);
  // The mesh's cluster tree: made the first time, kept after. Safe from any thread.
  std::shared_ptr<const ClusterTree> clusters(const Mesh& m);
  // Cooks many meshes at once, spread over every core (those already kept cost nothing).
  void cookAll(const std::vector<Mesh>& meshes);
  size_t kept() const;
  void forget();  // everything kept is dropped (tests)

 private:
  mutable std::mutex m_;
  std::unordered_map<uint64_t, std::shared_ptr<const ClusterTree>> trees_;
};

}  // namespace loop
