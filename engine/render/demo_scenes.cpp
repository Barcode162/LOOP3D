#include "engine/render/demo_scenes.h"

#include "engine/core/random.h"
#include "engine/geometry/plants.h"
#include "engine/geometry/terrain.h"

namespace loop {

namespace {

void studio(Scene& s) {
  // A still life on a wooden floor by a window at late afternoon.
  Material planks;
  planks.name = "oak floor";
  planks.baseColor = {0.55f, 0.36f, 0.2f};
  planks.roughness = 0.45f;
  planks.pattern.kind = Pattern::Kind::kPlanks;
  planks.pattern.colorB = {0.25f, 0.13f, 0.06f};
  planks.pattern.bump = 0.4f;
  planks.pattern.roughnessVariation = 0.3f;
  int floorM = s.addMaterial(planks);
  Material plaster;
  plaster.name = "plaster";
  plaster.baseColor = {0.78f, 0.74f, 0.68f};
  plaster.roughness = 0.9f;
  plaster.pattern.kind = Pattern::Kind::kNoise;
  plaster.pattern.scale = 3;
  plaster.pattern.colorB = {0.7f, 0.66f, 0.6f};
  plaster.pattern.bump = 0.3f;
  int wallM = s.addMaterial(plaster);
  Material marble;
  marble.baseColor = {0.92f, 0.91f, 0.88f};
  marble.roughness = 0.15f;
  marble.subsurface = 0.3f;
  marble.subsurfaceColor = {1, 0.9f, 0.8f};
  marble.pattern.kind = Pattern::Kind::kMarble;
  marble.pattern.scale = 2.5f;
  marble.pattern.colorB = {0.25f, 0.27f, 0.3f};
  int marbleM = s.addMaterial(marble);
  Material glass;
  glass.baseColor = Vec3(1);
  glass.transmission = 1, glass.roughness = 0, glass.ior = 1.52f, glass.dispersion = 1;
  int glassM = s.addMaterial(glass);
  Material gold;
  gold.baseColor = {1.0f, 0.78f, 0.34f};
  gold.metallic = 1, gold.roughness = 0.22f;
  int goldM = s.addMaterial(gold);
  Material paint;
  paint.baseColor = {0.6f, 0.03f, 0.03f};
  paint.roughness = 0.35f, paint.clearcoat = 1, paint.clearcoatRoughness = 0.03f;
  int paintM = s.addMaterial(paint);
  Material wax;
  wax.baseColor = {0.95f, 0.9f, 0.78f};
  wax.roughness = 0.5f, wax.subsurface = 0.8f, wax.subsurfaceColor = {1, 0.75f, 0.45f};
  int waxM = s.addMaterial(wax);
  Material flame;
  flame.emission = kelvin(1850) * 25000.0f;
  flame.baseColor = Vec3(0);
  int flameM = s.addMaterial(flame);
  Material velvet;
  velvet.baseColor = {0.08f, 0.12f, 0.3f};
  velvet.roughness = 0.8f, velvet.sheen = 1;
  int velvetM = s.addMaterial(velvet);

  s.add(s.addMesh(shapes::plane({12, 12})), floorM);
  // back and left walls, the window cut as a gap in the left wall
  s.add(s.addMesh(shapes::box({12, 4, 0.2f})), wallM, Mat4::translation({0, 2, -3}));
  s.add(s.addMesh(shapes::box({0.2f, 4, 2.4f})), wallM, Mat4::translation({-3, 2, -1.8f}));
  s.add(s.addMesh(shapes::box({0.2f, 4, 6})), wallM, Mat4::translation({-3, 2, 3.6f}));
  s.add(s.addMesh(shapes::box({0.2f, 1.0f, 1.2f})), wallM, Mat4::translation({-3, 0.5f, 0}));
  s.add(s.addMesh(shapes::box({0.2f, 1.4f, 1.2f})), wallM, Mat4::translation({-3, 3.3f, 0}));
  // a low table (velvet top) with the things on it
  s.add(s.addMesh(shapes::roundedBox({2.4f, 0.5f, 1.2f}, 0.06f, 4)), velvetM, Mat4::translation({0, 0.25f, -0.6f}));
  s.add(s.addMesh(shapes::sphere(0.28f, 96, 48)), marbleM, Mat4::translation({-0.65f, 0.78f, -0.5f}));
  s.add(s.addMesh(shapes::sphere(0.22f, 96, 48)), glassM, Mat4::translation({0.05f, 0.72f, -0.15f}));
  s.add(s.addMesh(shapes::torus(0.18f, 0.06f, 96, 48)), goldM, Mat4::trs({0.55f, 0.56f, -0.35f}, Quat::axisAngle({1, 0, 0}, 0.35f), Vec3(1)));
  s.add(s.addMesh(shapes::roundedBox({0.3f, 0.3f, 0.3f}, 0.04f, 4)), paintM, Mat4::trs({0.75f, 0.65f, -0.95f}, Quat::axisAngle({0, 1, 0}, 0.6f), Vec3(1)));
  s.add(s.addMesh(shapes::cylinder(0.05f, 0.3f, 48)), waxM, Mat4::translation({-0.15f, 0.65f, -0.95f}));
  s.add(s.addMesh(shapes::sphere(0.012f, 16, 8)), flameM, Mat4::trs({-0.15f, 0.83f, -0.95f}, Quat(), {1, 2.2f, 1}));
  Light candle;
  candle.position = {-0.15f, 0.84f, -0.95f};
  candle.lumens = 12, candle.color = kelvin(1850), candle.radius = 0.008f;
  s.lights.push_back(candle);

  Light sun;
  sun.kind = Light::Kind::kSun;
  sun.direction = normalize(Vec3{1, -0.42f, -0.35f});
  sun.lux = 100000;
  s.lights.push_back(sun);
  s.sky.kind = Sky::Kind::kPhysical;
  // a ceiling, and a little dust in the room's air for the sunbeam
  s.add(s.addMesh(shapes::box({12, 0.2f, 12})), wallM, Mat4::translation({0, 4.1f, 0}));
  s.add(s.addMesh(shapes::box({0.2f, 4, 12})), wallM, Mat4::translation({6, 2, 0}));
  s.add(s.addMesh(shapes::box({12, 4, 0.2f})), wallM, Mat4::translation({0, 2, 6}));
  s.fog.density = 0.006f, s.fog.anisotropy = 0.75f, s.fog.box = AABB({-3, 0, -3}, {6, 4, 6});

  Camera& c = s.camera;
  c.position = {0.4f, 1.15f, 2.4f};
  c.target = {0.0f, 0.62f, -0.5f};
  c.focalLength = 50, c.fStop = 2.2f;
  c.focusDistance = length(Vec3{0.05f, 0.72f, -0.15f} - c.position);
  lookByName("golden", s.look);
}

void valley(Scene& s) {
  // An eroded valley with a forest at sunrise, in haze.
  HeightMap hm(257, 1600);
  HeightMap::Shape sh;
  sh.height = 260, sh.seed = 4, sh.ridges = 0.65f;
  hm.generate(sh);
  HeightMap::Erosion er;
  er.droplets = 160000;
  hm.erodeHydraulic(er);
  hm.erodeThermal(8);
  Material ground;
  ground.baseColor = {0.16f, 0.22f, 0.07f};
  ground.roughness = 0.9f;
  ground.pattern.kind = Pattern::Kind::kGround;
  ground.pattern.colorB = {0.36f, 0.33f, 0.29f};
  ground.pattern.bump = 2;
  int groundM = s.addMaterial(ground);
  s.add(s.addMesh(hm.toMesh(1)), groundM);
  Material bark;
  bark.baseColor = {0.18f, 0.12f, 0.08f};
  bark.roughness = 0.9f;
  int barkM = s.addMaterial(bark);
  Material needles;
  needles.baseColor = {0.06f, 0.14f, 0.05f};
  needles.roughness = 0.7f, needles.translucency = 0.35f, needles.leafShape = true;
  int leafM = s.addMaterial(needles);
  TreeShape ts;
  ts.conifer = true, ts.height = 14, ts.levels = 2, ts.branches = 9, ts.leavesPerTip = 6, ts.leafSize = 0.5f;
  std::vector<std::pair<int, int>> kinds;
  for (int k = 0; k < 4; ++k) {
    ts.seed = k + 1;
    TreeMeshes t = growTree(ts);
    kinds.push_back({s.addMesh(std::move(t.bark)), s.addMesh(std::move(t.leaves))});
  }
  Rng rng(9);
  auto spots = poissonDisk(700, 9, 3, [&](float x, float z) { return hm.slope(x, z) < 0.25f && hm.height(x, z) < 150; });
  for (Vec2 p : spots) {
    if (length(p - Vec2(0, 300)) < 40) continue;
    auto [b, l] = kinds[rng.below(4)];
    float y = hm.height(p.x, p.y) - 0.3f;
    Mat4 xf = Mat4::trs({p.x, y, p.y}, Quat::axisAngle({0, 1, 0}, rng.uniform() * kTwoPi), Vec3(0.7f + 0.6f * rng.uniform()));
    s.add(b, barkM, xf), s.add(l, leafM, xf);
  }
  Light sun;
  sun.kind = Light::Kind::kSun;
  sun.direction = normalize(Vec3{-0.3f, -0.12f, -1});
  sun.lux = 110000;
  s.lights.push_back(sun);
  s.sky.turbidity = 4;
  s.fog.density = 0.0012f, s.fog.top = 120, s.fog.anisotropy = 0.75f;
  Camera& c = s.camera;
  Vec2 cp{30, 420};
  c.position = {cp.x, hm.height(cp.x, cp.y) + 40, cp.y};
  c.target = {0, 60, -200};
  c.focalLength = 28, c.fStop = 8;
  lookByName("natural", s.look);
}

}  // namespace

bool buildDemoScene(const std::string& name, Scene& s) {
  s = Scene();
  if (name == "studio") return studio(s), true;
  if (name == "valley") return valley(s), true;
  return false;
}

}  // namespace loop
