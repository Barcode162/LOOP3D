#include "engine/render/detail_steps.h"

#include <algorithm>

#include "engine/core/settings.h"

namespace loop {
namespace {

// One row a step. Every row names the same settings, so moving the dial
// always sets each of them (nothing is left over from the step before).
DetailStep row(int step, int quality, const char* says, float ratio, float floor, int shading, int lampMaps,
               int lampMapSize, int lamps, int sunMap, float shadowDistance, bool mirror, bool ssr, float ao,
               bool lightCache, int probes, float clusterError) {
  DetailStep s;
  s.step = step;
  s.quality = quality;
  s.says = says;
  s.values = {
      {"render.ratioPhone", Json(ratio)},       {"render.ratioHigh", Json(ratio)},
      {"render.floor", Json(floor)},            {"render.shading", Json(shading)},
      {"render.lampMaps", Json(lampMaps)},      {"render.lampMapSize", Json(lampMapSize)},
      {"render.lamps", Json(lamps)},            {"render.sunMapPhone", Json(std::min(sunMap, 4096))},
      {"render.sunMapHigh", Json(std::max(sunMap, 1024))},
      {"render.shadowDistance", Json(shadowDistance)},
      {"render.mirrorPane", Json(mirror)},      {"render.ssr", Json(ssr)},
      {"render.ao", Json(ao)},                  {"render.lightCache", Json(lightCache)},
      {"render.lightCacheProbes", Json(probes)}, {"render.clusterError", Json(clusterError)},
  };
  return s;
}

const std::vector<DetailStep>& table() {
  //          step q  says                                                        ratio floor shade maps size lamps sun  reach mirror ssr  ao   cache probes shapes
  static const std::vector<DetailStep> t = {
      row(1, 0, "Smoothest: no lamp shadows, simpler far shapes, always sharp"   , 1.0f, 0.9f, 0, 0, 128, 0, 512, 20, false, false, 0.0f, false, 512, 4.0f),
      row(2, 0, "Soft creases come in; a finer sun shadow", 1.25f, 0.85f, 0, 0, 128, 0, 1024, 20, false, false, 0.5f, false, 512, 3.0f),
      row(3, 0, "The brightest lamp casts a soft shadow; bounced light comes in",  1.5f, 0.75f, 0, 1, 128, 0, 1024, 25, false, false, 1.0f, true, 1024, 2.0f),
      row(4, 0, "Every layer of a material lit; the glass mirrors the room",       2.0f, 0.65f, 1, 1, 256, 0, 1024, 25, true, false, 1.0f, true, 1024, 1.5f),
      row(5, 0, "Phone: two lamps cast shadows, full shape detail",                2.0f, 0.5f, 1, 2, 256, 0, 1024, 25, true, false, 1.0f, true, 2048, 1.0f),
      row(6, 0, "Sharper shadows from lamps and sun, finer creases",               2.0f, 0.5f, 2, 2, 512, 0, 2048, 30, true, false, 1.0f, true, 2048, 1.0f),
      row(7, 1, "High: every lamp casts shadows, traced; screen reflections",      2.0f, 0.5f, 2, 2, 256, 2, 2048, 40, true, true, 1.0f, true, 2048, 1.0f),
      row(8, 1, "Four lamps traced on their own; shapes a little finer",           2.0f, 0.5f, 2, 2, 256, 4, 2048, 40, true, true, 1.0f, true, 2048, 0.75f),
      row(9, 1, "Eight lamps on their own, the most shader detail, sharp sun",     2.0f, 0.45f, 3, 2, 256, 8, 4096, 50, true, true, 1.0f, true, 4096, 0.5f),
      row(10, 1, "Most: sixteen lamps on their own, sun shadows reaching far",      2.0f, 0.35f, 3, 2, 512, 16, 4096, 60, true, true, 1.0f, true, 4096, 0.5f),
  };
  return t;
}

}  // namespace

const DetailStep& detailStep(int step) { return table()[std::clamp(step, 1, kDetailSteps) - 1]; }

int applyDetail(int step) {
  const DetailStep& s = detailStep(step);
  Settings& st = Settings::get();
  st.clear(Settings::kDetail);
  for (const auto& [id, v] : s.values) st.set(Settings::kDetail, id, v);
  return s.quality;
}

}  // namespace loop
