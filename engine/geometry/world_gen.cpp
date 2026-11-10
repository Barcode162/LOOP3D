#include "engine/geometry/world_gen.h"

#include <algorithm>
#include <cmath>
#include <queue>

#include "engine/core/random.h"
#include "engine/geometry/noise.h"

namespace loop {

namespace {
const int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
const int kDz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
const float kDist[8] = {1, 1, 1, 1, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f};

// Priority-flood (Barnes et al. 2014) from the outlets (the map's edge and the
// sea): every cell is reached from the lowest way in, which is where its water
// goes out. `order` is the order reached (each cell after the one it drains
// to); `level` the water's level (a hollow filled to its rim); with `rise`, a
// filled hollow slopes a hair towards its outlet so water keeps moving.
void flood(const WorldMap& m, const std::vector<float>& h, float rise, std::vector<int>& down, std::vector<int>& order, std::vector<float>& level) {
  const int n = m.n;
  const size_t N = h.size();
  down.assign(N, -1), order.clear(), level = h;
  std::vector<uint8_t> seen(N, 0);
  using Item = std::pair<float, int>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
  for (int z = 0; z < n; ++z)
    for (int x = 0; x < n; ++x) {
      const size_t i = m.at(x, z);
      if (x == 0 || z == 0 || x == n - 1 || z == n - 1 || h[i] <= 0) q.push({h[i], static_cast<int>(i)}), seen[i] = 1;
    }
  while (!q.empty()) {
    const auto [lv, i] = q.top();
    q.pop();
    order.push_back(i);
    const int x = i % n, z = i / n;
    for (int k = 0; k < 8; ++k) {
      const int qx = x + kDx[k], qz = z + kDz[k];
      if (qx < 0 || qz < 0 || qx >= n || qz >= n) continue;
      const size_t j = m.at(qx, qz);
      if (seen[j]) continue;
      seen[j] = 1;
      level[j] = std::max(h[j], lv + rise);
      down[j] = i;
      q.push({level[j], static_cast<int>(j)});
    }
  }
}
}  // namespace

float WorldMap::sample(const std::vector<float>& f, float x, float z) const {
  const float gx = std::clamp((x / size + 0.5f) * (n - 1), 0.0f, static_cast<float>(n - 1)),
              gz = std::clamp((z / size + 0.5f) * (n - 1), 0.0f, static_cast<float>(n - 1));
  const int x0 = std::min(static_cast<int>(gx), n - 2), z0 = std::min(static_cast<int>(gz), n - 2);
  const float tx = gx - x0, tz = gz - z0;
  return lerp(lerp(f[at(x0, z0)], f[at(x0 + 1, z0)], tx), lerp(f[at(x0, z0 + 1)], f[at(x0 + 1, z0 + 1)], tx), tz);
}
float WorldMap::heightAt(float x, float z) const { return sample(height, x, z); }
uint8_t WorldMap::groundAt(float x, float z) const {
  const int gx = std::clamp(static_cast<int>(std::lround((x / size + 0.5f) * (n - 1))), 0, n - 1),
            gz = std::clamp(static_cast<int>(std::lround((z / size + 0.5f) * (n - 1))), 0, n - 1);
  return ground[at(gx, gz)];
}
float WorldMap::slopeAt(int x, int z) const {
  float s = 0;
  const float h0 = height[at(x, z)];
  for (int k = 0; k < 8; ++k) {
    const int qx = x + kDx[k], qz = z + kDz[k];
    if (qx < 0 || qz < 0 || qx >= n || qz >= n) continue;
    s = std::max(s, std::fabs(h0 - height[at(qx, qz)]) / (kDist[k] * cell));
  }
  return s;
}

WorldMap generateWorld(const WorldSettings& s) {
  WorldMap m;
  m.n = std::max(s.cells, 9);
  m.size = s.size;
  m.cell = s.size / static_cast<float>(m.n - 1);
  const int n = m.n;
  const size_t N = static_cast<size_t>(n) * n;
  Rng rng(s.seed * 7919ull + 13);

  // 1. plates: each a centre, land or sea, and a drift
  struct Plate { Vec2 c, v; bool land; };
  std::vector<Plate> plates;
  const int P = std::max(s.plates, 2);
  for (int k = 0; k < P; ++k) {
    Plate p;
    p.c = {rng.range(-0.45f, 0.45f) * s.size, rng.range(-0.45f, 0.45f) * s.size};
    const float a = rng.uniform() * kTwoPi;
    p.v = {std::cos(a), std::sin(a)};
    p.land = k < std::max(1, static_cast<int>(std::lround(s.landShare * P)));
    plates.push_back(p);
  }
  noise::Fractal warpF;
  warpF.octaves = 4, warpF.seed = s.seed * 31 + 5;
  m.plate.assign(N, 0);
  std::vector<float> uplift(N, 0), h(N, 0);
  const float edgeWidth = 0.08f * s.size;  // the land fades into the sea round the edge
  for (int z = 0; z < n; ++z)
    for (int x = 0; x < n; ++x) {
      const size_t i = m.at(x, z);
      const Vec2 p = m.place(x, z);
      // plate edges wander (the distance warped by noise)
      const Vec2 w = p + Vec2(noise::fbm(p / (s.size * 0.3f), warpF), noise::fbm(p / (s.size * 0.3f) + Vec2(5.2f, 1.3f), warpF)) * (s.size * 0.25f);
      int a = 0, b = 1;
      float da = 1e30f, db = 1e30f;
      for (int k = 0; k < P; ++k) {
        const float d = length(w - plates[static_cast<size_t>(k)].c);
        if (d < da) db = da, b = a, da = d, a = k;
        else if (d < db) db = d, b = k;
      }
      m.plate[i] = static_cast<uint8_t>(a);
      const Plate& A = plates[static_cast<size_t>(a)];
      const Plate& B = plates[static_cast<size_t>(b)];
      // how hard they meet: their drifts along the line between them
      const Vec2 across = normalize(B.c - A.c);
      const float meet = dot(A.v - B.v, across);  // > 0 pushing together
      const float near = std::exp(-sqr((db - da) / (0.12f * s.size)));
      // the land's own slow rise, uneven (hills; rivers wander round them)
      const float base = A.land ? 3.0f * (1.0f + 0.9f * noise::fbm(p / (s.size * 0.08f) + Vec2(9.1f, 2.7f), warpF)) : 0.0f;
      // ranges rise unevenly along their length: peaks and passes
      const float ridges = 0.45f + 1.1f * std::max(0.0f, 0.5f + noise::fbm(p / (s.size * 0.06f) + Vec2(2.2f, 6.6f), warpF));
      float u = base + near * meet * 90.0f * s.mountains * ridges * (A.land || B.land ? 1.0f : 0.3f);
      if (meet < 0) u = base + near * meet * 10.0f;  // pulling apart: a rift
      // the land sinks into the open sea well inside the map's edge, along a wandering line
      const float edge = s.size * 0.47f - length(p) + noise::fbm(p / (s.size * 0.2f) + Vec2(1.7f, 4.4f), warpF) * s.size * 0.12f;
      const float square = std::min({p.x + s.size * 0.5f, s.size * 0.5f - p.x, p.y + s.size * 0.5f, s.size * 0.5f - p.y});
      const float inland = std::clamp(std::min(edge, square - edgeWidth * 0.5f) / edgeWidth, 0.0f, 1.0f);
      uplift[i] = std::max(u, 0.0f) * inland;
      // the coast wanders: land plates stand high, sea plates low, with broad swells and bays in both
      const float swell = noise::fbm(p / (s.size * 0.15f) + Vec2(3.7f, 8.2f), warpF) * 160.0f + noise::fbm(p / (s.size * 0.04f), warpF) * 30.0f;
      h[i] = (A.land ? 60.0f : -120.0f) * inland + (1 - inland) * -300.0f + swell * inland;
    }

  // 2. rivers carving while the land rises (stream power, n = 1, m = 0.5)
  std::vector<int> down, order;
  std::vector<float> level, area(N);
  const float K = 0.6f;
  for (int age = 0; age < s.ages; ++age) {
    flood(m, h, 1e-3f, down, order, level);
    std::fill(area.begin(), area.end(), 1.0f);
    for (size_t k = order.size(); k-- > 0;) {
      const int i = order[k];
      if (down[static_cast<size_t>(i)] >= 0) area[static_cast<size_t>(down[static_cast<size_t>(i)])] += area[static_cast<size_t>(i)];
    }
    for (int i : order) {
      const size_t si = static_cast<size_t>(i);
      const int r = down[si];
      if (r < 0) continue;  // an outlet: the sea holds it
      const float dist = (i % n != r % n && i / n != r / n) ? 1.41421356f : 1.0f;
      const float F = K * std::sqrt(area[si]) / dist;
      // rivers cut down to the level they flow into: the sea's surface, not its floor
      h[si] = (h[si] + uplift[si] + F * std::max(h[static_cast<size_t>(r)], 0.0f)) / (1 + F);
    }
  }

  // 3. slopes rest at loose rock's angle (35 degrees) at most
  const float talus = std::tan(35.0f * kPi / 180.0f) * m.cell;
  for (int it = 0; it < 400; ++it) {
    bool moved = false;
    for (int z = 0; z < n; ++z)
      for (int x = 0; x < n; ++x) {
        const size_t i = m.at(x, z);
        for (int k = 0; k < 8; ++k) {
          const int qx = x + kDx[k], qz = z + kDz[k];
          if (qx < 0 || qz < 0 || qx >= n || qz >= n) continue;
          const size_t j = m.at(qx, qz);
          const float over = h[i] - h[j] - talus * kDist[k];
          if (over > 0.01f) {
            h[i] -= over * 0.5f, h[j] += over * 0.5f;
            moved = true;
          }
        }
      }
    if (!moved) break;
  }
  m.height = h;

  // 4. lakes, and where each cell's water goes
  flood(m, h, 0, down, order, level);
  m.water = level;
  m.down = down;
  m.flow.assign(N, m.cell * m.cell);
  for (size_t k = order.size(); k-- > 0;) {
    const int i = order[k];
    if (down[static_cast<size_t>(i)] >= 0) m.flow[static_cast<size_t>(down[static_cast<size_t>(i)])] += m.flow[static_cast<size_t>(i)];
  }
  for (size_t i = 0; i < N; ++i)
    if (h[i] <= 0) m.water[i] = std::max(m.water[i], 0.0f);

  // 5. climate: warmth by height and by north; rain from wet air, wrung out on the climb
  m.warmth.assign(N, 0), m.rain.assign(N, 0);
  const Vec2 wind(std::cos(s.wind), std::sin(s.wind));  // the way the air moves
  std::vector<int> byWind(N);
  for (size_t i = 0; i < N; ++i) byWind[i] = static_cast<int>(i);
  std::sort(byWind.begin(), byWind.end(), [&](int a, int b) {
    return dot(m.place(a % n, a / n), wind) < dot(m.place(b % n, b / n), wind);
  });
  std::vector<float> wet(N, 1);
  float most = 1e-6f;
  for (int i : byWind) {
    const size_t si = static_cast<size_t>(i);
    const int x = i % n, z = i / n;
    const int ux = static_cast<int>(std::lround(x - wind.x)), uz = static_cast<int>(std::lround(z - wind.y));
    const bool edge = ux < 0 || uz < 0 || ux >= n || uz >= n;
    const float up = edge ? 1.0f : wet[m.at(ux, uz)];
    const float hUp = edge ? 0.0f : std::max(m.water[m.at(ux, uz)], 0.0f);
    const float here = std::max(m.water[si], 0.0f);
    if (h[si] <= 0 || m.water[si] > h[si] + 0.5f) {  // over water: the air takes up more
      wet[si] = std::min(1.0f, up + 0.08f);
      m.rain[si] = 0.04f * wet[si];
    } else {
      const float climb = std::max(0.0f, here - hUp);
      const float r = std::min(up, up * (0.012f + climb / 250.0f));
      m.rain[si] = r;
      wet[si] = std::min(1.0f, up - r * 0.6f + 0.006f);  // the land breathes a little back (plants, lakes)
      most = std::max(most, r);
    }
    m.warmth[si] = s.warmth - 6.5f * here / 1000.0f - 8.0f * (m.place(x, z).y / s.size);
  }
  // the air mixes sideways as it goes: no stripes from one line of air to the next
  for (int pass = 0; pass < 4; ++pass) {
    std::vector<float> r2 = m.rain;
    for (int z = 1; z < n - 1; ++z)
      for (int x = 1; x < n - 1; ++x) {
        float sum = 0;
        for (int dz = -1; dz <= 1; ++dz)
          for (int dx = -1; dx <= 1; ++dx) sum += m.rain[m.at(x + dx, z + dz)];
        r2[m.at(x, z)] = sum / 9;
      }
    m.rain.swap(r2);
  }
  // in 0..1 against the wettest tenth of the land
  std::vector<float> landRain;
  for (size_t i = 0; i < N; ++i)
    if (h[i] > 0) landRain.push_back(m.rain[i]);
  if (!landRain.empty()) {
    std::nth_element(landRain.begin(), landRain.begin() + static_cast<long>(landRain.size() * 9 / 10), landRain.end());
    most = std::max(landRain[landRain.size() * 9 / 10], 1e-6f);
  }
  for (float& r : m.rain) r = std::clamp(r / most, 0.0f, 1.0f);

  // 6. the ground, and the forest
  m.ground.assign(N, WorldMap::kGrass), m.forest.assign(N, 0);
  for (int z = 0; z < n; ++z)
    for (int x = 0; x < n; ++x) {
      const size_t i = m.at(x, z);
      const float slope = m.slopeAt(x, z), T = m.warmth[i];
      uint8_t g;
      if (h[i] <= 0) g = WorldMap::kSea;
      else if (m.water[i] > h[i] + 0.5f) g = WorldMap::kLake;
      else {
        bool shore = false;
        for (int k = 0; k < 8 && h[i] < 6; ++k) {
          const int qx = x + kDx[k] * 2, qz = z + kDz[k] * 2;
          if (qx >= 0 && qz >= 0 && qx < n && qz < n && h[m.at(qx, qz)] <= 0) shore = true;
        }
        const float grow = smoothstep(0.12f, 0.35f, m.rain[i]) * smoothstep(1.0f, 6.0f, T) * (1 - smoothstep(0.5f, 0.68f, slope));
        m.forest[i] = grow;
        if (shore) g = WorldMap::kSand;
        else if (T < -1) g = WorldMap::kSnow;
        else if ((slope > 0.62f && h[i] > 250) || slope > 0.69f || T < 1) g = WorldMap::kRock;  // bare where high and steep
        else if (grow > 0.35f) g = WorldMap::kForest;
        else g = WorldMap::kGrass;
      }
      if (g != WorldMap::kForest && g != WorldMap::kGrass) m.forest[i] = 0;
      m.ground[i] = g;
    }
  return m;
}

}  // namespace loop
