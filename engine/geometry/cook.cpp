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

std::shared_ptr<const ClusterTree> Cook::clusters(const Mesh& m) { return clusters(m, false); }

std::shared_ptr<const ClusterTree> Cook::clusters(const Mesh& m, bool parallel) {
  uint64_t key = fingerprint(m);
  {
    std::lock_guard<std::mutex> l(m_);
    auto it = trees_.find(key);
    if (it != trees_.end()) return it->second;
  }
  auto tree = std::make_shared<const ClusterTree>(buildClusterTree(m, 128, parallel));
  std::lock_guard<std::mutex> l(m_);
  return trees_.emplace(key, std::move(tree)).first->second;  // another thread may have kept one first: the same
}

std::shared_ptr<const Marks> Cook::marks(const Mesh& m) {
  uint64_t key = fingerprint(m);
  {
    std::lock_guard<std::mutex> l(m_);
    auto it = marks_.find(key);
    if (it != marks_.end()) return it->second;
  }
  auto made = std::make_shared<const Marks>(measureMarks(m));
  std::lock_guard<std::mutex> l(m_);
  return marks_.emplace(key, std::move(made)).first->second;  // another thread may have kept one first: the same
}

void Cook::cookAll(const std::vector<Mesh>& meshes) {
  std::vector<const Mesh*> all;
  for (const Mesh& m : meshes) all.push_back(&m);
  cookAll(all);
}

void Cook::cookAll(const std::vector<const Mesh*>& meshes) {
  // a big mesh (the land, a meadow) alone, its groups over every core; the
  // many small ones side by side, one core each, the biggest first so no
  // core is left with a big one at the end
  std::vector<const Mesh*> order(meshes);
  std::sort(order.begin(), order.end(), [](const Mesh* a, const Mesh* b) { return a->indices.size() > b->indices.size(); });
  size_t total = 0, big = 0;
  for (const Mesh* m : order) total += m->triangleCount();
  // big: a tenth of the scene or more (and 50,000 triangles): it would hold
  // up the others' cores if one core cooked it
  while (big < order.size() && order[big]->triangleCount() >= std::max<size_t>(50000, total / 10)) clusters(*order[big++], true);
  JobSystem::shared().parallelFor(static_cast<int>(order.size() - big), [&](int k) { clusters(*order[big + k], false); });
}

size_t Cook::kept() const {
  std::lock_guard<std::mutex> l(m_);
  return trees_.size() + marks_.size();
}

void Cook::forget() {
  std::lock_guard<std::mutex> l(m_);
  trees_.clear();
  marks_.clear();
}

}  // namespace loop
