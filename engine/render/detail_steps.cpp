#include "engine/render/detail_steps.h"

#include <algorithm>

#include "engine/core/settings.h"

namespace loop {
namespace {

// One row a step. Every row of every recipe names the same settings, so moving
// the dial always sets each of them (nothing is left over from the step before).
DetailStep row(int step, int quality, const char* says, float ratio, float floor, int shading, int lampMaps,
               int lampMapSize, int lamps, int sunMap, float shadowDistance, bool mirror, bool ssr, float ao,
               bool lightCache, int probes, float clusterError, float lampGrouping) {
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
      {"render.lampGrouping", Json(lampGrouping)},
  };
  return s;
}

// Columns: step, quality, says | ratio floor shading lampMaps lampMapSize lamps sunMap reach mirror ssr ao
// bouncedLight probes shapes(clusterError) lampGrouping

const std::vector<DetailStep>& balanced() {
  static const std::vector<DetailStep> t = {
      row(1, 0, "Smoothest: no lamp shadows, simpler far shapes, always sharp", 1.0f, 0.9f, 0, 0, 128, 0, 512, 20, false, false, 0.0f, false, 512, 4.0f, 7),
      row(2, 0, "Soft creases come in; a finer sun shadow", 1.25f, 0.85f, 0, 0, 128, 0, 1024, 20, false, false, 0.5f, false, 512, 3.0f, 7),
      row(3, 0, "The brightest lamp casts a soft shadow; bounced light comes in", 1.5f, 0.75f, 0, 1, 128, 0, 1024, 25, false, false, 1.0f, true, 1024, 2.0f, 7),
      row(4, 0, "Every layer of a material lit; the glass mirrors the room", 2.0f, 0.65f, 1, 1, 256, 0, 1024, 25, true, false, 1.0f, true, 1024, 1.5f, 7),
      row(5, 0, "Phone: two lamps cast shadows, full shape detail", 2.0f, 0.5f, 1, 2, 256, 0, 1024, 25, true, false, 1.0f, true, 2048, 1.0f, 7),
      row(6, 0, "Sharper shadows from lamps and sun, finer creases", 2.0f, 0.5f, 2, 2, 512, 0, 2048, 30, true, false, 1.0f, true, 2048, 1.0f, 7),
      row(7, 1, "High: every lamp casts shadows, traced; screen reflections", 2.0f, 0.5f, 2, 2, 256, 2, 2048, 40, true, true, 1.0f, true, 2048, 1.0f, 7),
      row(8, 1, "Four lamps traced on their own; shapes a little finer", 2.0f, 0.5f, 2, 2, 256, 4, 2048, 40, true, true, 1.0f, true, 2048, 0.75f, 7),
      row(9, 1, "Eight lamps on their own, the most shader detail, sharp sun", 2.0f, 0.45f, 3, 2, 256, 8, 4096, 50, true, true, 1.0f, true, 4096, 0.5f, 7),
      row(10, 1, "Most: sixteen lamps on their own, sun shadows reaching far", 2.0f, 0.35f, 3, 2, 512, 16, 4096, 60, true, true, 1.0f, true, 4096, 0.5f, 7),
  };
  return t;
}

// A night room: the lamps are the cost (the Loft: 46 lamps, 34 lamps' work a
// pixel against 24), its shapes are cheap (0.07 M triangles). Low steps group
// far lamps sooner and drop lamp shadows; shapes stay at full detail and the
// sun's shadow (hardly seen at night) stays small throughout.
const std::vector<DetailStep>& interiorRows() {
  static const std::vector<DetailStep> t = {
      row(1, 0, "Smoothest room: far lamps worked out in groups, no lamp shadows, always sharp", 1.25f, 0.9f, 0, 0, 128, 0, 512, 15, false, false, 0.5f, false, 512, 1.0f, 3),
      row(2, 0, "The brightest lamp casts a soft shadow; creases in the corners", 1.5f, 0.85f, 0, 1, 128, 0, 512, 15, false, false, 1.0f, false, 512, 1.0f, 4),
      row(3, 0, "Light bounces off the walls; a sharper picture", 2.0f, 0.75f, 0, 1, 256, 0, 512, 15, false, false, 1.0f, true, 1024, 1.0f, 5),
      row(4, 0, "Two lamps cast shadows; every layer of a material lit; the glass mirrors the room", 2.0f, 0.65f, 1, 2, 256, 0, 512, 15, true, false, 1.0f, true, 1024, 1.0f, 6),
      row(5, 0, "Phone: more lamps worked out one by one near you", 2.0f, 0.5f, 1, 2, 256, 0, 1024, 20, true, false, 1.0f, true, 2048, 1.0f, 7),
      row(6, 0, "Sharper lamp shadows, finer creases", 2.0f, 0.5f, 2, 2, 512, 0, 1024, 20, true, false, 1.0f, true, 2048, 1.0f, 8),
      row(7, 1, "High: every lamp casts shadows, traced; screen reflections", 2.0f, 0.5f, 2, 2, 256, 2, 1024, 20, true, true, 1.0f, true, 2048, 1.0f, 8),
      row(8, 1, "Four lamps traced on their own; finer bounced light", 2.0f, 0.5f, 2, 2, 256, 4, 2048, 25, true, true, 1.0f, true, 4096, 0.75f, 10),
      row(9, 1, "Eight lamps on their own, the most shader detail", 2.0f, 0.45f, 3, 2, 256, 8, 2048, 25, true, true, 1.0f, true, 4096, 0.5f, 14),
      row(10, 1, "Most: sixteen lamps on their own, nearly every lamp worked out alone", 2.0f, 0.35f, 3, 2, 512, 16, 2048, 30, true, true, 1.0f, true, 8192, 0.5f, 20),
  };
  return t;
}

// A daylight space: shapes and the sun's shadow are the cost (the Glade: 3.86 M
// triangles against 0.6, the sun's shadow drawing more clusters than the view).
// Low steps make far shapes simpler and the sun's shadow shorter and coarser;
// high steps spend on a sharp sun shadow reaching far.
const std::vector<DetailStep>& openRows() {
  static const std::vector<DetailStep> t = {
      row(1, 0, "Smoothest outdoors: simpler far shapes, a short sun shadow, always sharp", 1.0f, 0.9f, 0, 0, 128, 0, 512, 15, false, false, 0.0f, false, 512, 6.0f, 4),
      row(2, 0, "Shapes a little finer; soft creases", 1.25f, 0.85f, 0, 0, 128, 0, 512, 20, false, false, 0.5f, false, 512, 4.0f, 5),
      row(3, 0, "A finer sun shadow; light bounces off the ground", 1.5f, 0.75f, 0, 1, 128, 0, 1024, 20, false, false, 1.0f, true, 1024, 3.0f, 5),
      row(4, 0, "Every layer of a material lit; shapes finer again", 2.0f, 0.65f, 1, 1, 256, 0, 1024, 25, true, false, 1.0f, true, 1024, 2.0f, 6),
      row(5, 0, "Phone: always sharp, the sun's shadow to 25 m", 2.0f, 0.5f, 1, 2, 256, 0, 1024, 25, true, false, 1.0f, true, 2048, 1.5f, 7),
      row(6, 0, "The sun's shadow sharper and reaching 35 m; full shape detail", 2.0f, 0.5f, 2, 2, 256, 0, 2048, 35, true, false, 1.0f, true, 2048, 1.0f, 7),
      row(7, 1, "High: the sun's shadow to 50 m; screen reflections", 2.0f, 0.5f, 2, 2, 256, 2, 2048, 50, true, true, 1.0f, true, 2048, 1.0f, 7),
      row(8, 1, "A sharper sun shadow reaching 70 m; finer shapes", 2.0f, 0.5f, 2, 2, 256, 2, 4096, 70, true, true, 1.0f, true, 2048, 0.75f, 7),
      row(9, 1, "The most shader detail; the sun's shadow to 100 m", 2.0f, 0.45f, 3, 2, 256, 4, 4096, 100, true, true, 1.0f, true, 4096, 0.5f, 7),
      row(10, 1, "Most: the sun's shadow to 150 m at its sharpest", 2.0f, 0.35f, 3, 2, 256, 8, 8192, 150, true, true, 1.0f, true, 4096, 0.5f, 7),
  };
  return t;
}

const std::vector<DetailStep>& table(DetailStyle style) {
  switch (style) {
    case DetailStyle::kInterior: return interiorRows();
    case DetailStyle::kOpen: return openRows();
    default: return balanced();
  }
}

}  // namespace

const char* detailStyleName(DetailStyle style) {
  switch (style) {
    case DetailStyle::kInterior: return "interior";
    case DetailStyle::kOpen: return "open";
    default: return "balanced";
  }
}

DetailStyle detailStyleFor(const std::string& named, int lamps, bool sun) {
  if (named == "interior") return DetailStyle::kInterior;
  if (named == "open") return DetailStyle::kOpen;
  if (named == "balanced") return DetailStyle::kBalanced;
  if (lamps >= 6 || (!sun && lamps > 0)) return DetailStyle::kInterior;
  if (sun) return DetailStyle::kOpen;
  return DetailStyle::kBalanced;
}

const DetailStep& detailStep(int step, DetailStyle style) {
  return table(style)[std::clamp(step, 1, kDetailSteps) - 1];
}

int applyDetail(int step, DetailStyle style) {
  const DetailStep& s = detailStep(step, style);
  Settings& st = Settings::get();
  st.clear(Settings::kDetail);
  for (const auto& [id, v] : s.values) st.set(Settings::kDetail, id, v);
  return s.quality;
}

}  // namespace loop
