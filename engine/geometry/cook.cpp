#include "engine/geometry/cook.h"

#include <algorithm>
#include <cstring>

#include "engine/core/jobs.h"

namespace loop {

Cook& Cook::shared() {
  static Cook c;
  return c;
}

uint64_t Cook::fingerprint(const Mesh& m) {
  uint64_t h = 1469598103934665603ull;
  auto add = [&](const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  };
  uint64_t counts[2] = {m.positions.size(), m.indices.size()};
  add(counts, sizeof counts);
  if (!m.positions.empty()) add(m.positions.data(), m.positions.size() * sizeof(Vec3));
  if (!m.indices.empty()) add(m.indices.data(), m.indices.size() * sizeof(uint32_t));
  return h;
}

std::shared_ptr<const ClusterTree> Cook::clusters(const Mesh& m) {
  uint64_t key = fingerprint(m);
  {
    std::lock_guard<std::mutex> l(m_);
    auto it = trees_.find(key);
    if (it != trees_.end()) return it->second;
  }
  auto tree = std::make_shared<const ClusterTree>(buildClusterTree(m));
  std::lock_guard<std::mutex> l(m_);
  return trees_.emplace(key, std::move(tree)).first->second;  // another thread may have kept one first: the same
}

void Cook::cookAll(const std::vector<Mesh>& meshes) {
  std::vector<const Mesh*> all;
  for (const Mesh& m : meshes) all.push_back(&m);
  cookAll(all);
}

void Cook::cookAll(const std::vector<const Mesh*>& meshes) {
  // the biggest first, so no core is left with a big one at the end
  std::vector<const Mesh*> order(meshes);
  std::sort(order.begin(), order.end(), [](const Mesh* a, const Mesh* b) { return a->indices.size() > b->indices.size(); });
  JobSystem::shared().parallelFor(static_cast<int>(order.size()), [&](int k) { clusters(*order[k]); });
}

size_t Cook::kept() const {
  std::lock_guard<std::mutex> l(m_);
  return trees_.size();
}

void Cook::forget() {
  std::lock_guard<std::mutex> l(m_);
  trees_.clear();
}

}  // namespace loop
