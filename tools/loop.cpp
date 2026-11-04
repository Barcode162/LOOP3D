// `loop`: the engine's command line.
//   loop demo <name> [--look L] [--size WxH] [--samples N] [-o out.png]
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/core/log.h"
#include "engine/render/demo_scenes.h"
#include "engine/render/film.h"
#include "engine/render/pathtracer.h"

using namespace loop;

static int usage() {
  std::puts("loop demo <studio|valley> [--look name] [--size 1280x720] [--samples 64] [-o out.png]");
  return 1;
}

int main(int argc, char** argv) {
  if (argc < 3 || std::strcmp(argv[1], "demo") != 0) return usage();
  std::string name = argv[2], lookName, out = name + ".png";
  int w = 960, h = 540, spp = 32;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--look" && i + 1 < argc) lookName = argv[++i];
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &w, &h);
    else if (a == "--samples" && i + 1 < argc) spp = std::atoi(argv[++i]);
    else if (a == "-o" && i + 1 < argc) out = argv[++i];
  }
  Scene scene;
  if (!buildDemoScene(name, scene)) return usage();
  if (!lookName.empty() && !lookByName(lookName, scene.look)) {
    std::printf("unknown look %s\n", lookName.c_str());
    return 1;
  }
  PathTracer pt;
  pt.setScene(scene);
  RenderSettings rs;
  rs.width = w, rs.height = h, rs.samples = spp;
  rs.progress = [](float p) { std::fprintf(stderr, "\r%3d%%", static_cast<int>(p * 100)); return true; };
  RenderOutput r = pt.render(rs);
  FilmResult info;
  Image pic = develop(r.color, scene.camera, scene.look, 0, &info);
  writePng(out, pic, true);
  std::fprintf(stderr, "\r%s: %dx%d, %d samples, %.1f s, %.1f M rays/s, EV %.1f\n", out.c_str(), w, h, spp, r.seconds,
               r.rays / r.seconds / 1e6, info.ev100);
  return 0;
}
