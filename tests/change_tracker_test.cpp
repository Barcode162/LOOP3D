// The change tracker (engine/render/change_tracker.h): work redone only where
// something changed.
#include "engine/render/change_tracker.h"
#include "tests/test.h"

using namespace loop;

TEST(change_tracker_makes_again_only_when_something_changed) {
  ChangeTracker t;
  ChangeTracker::Made shadow;
  const std::vector<std::string> from = {"placements", "sun", "sun view"};
  CHECK(t.stale(shadow, from));  // never made
  t.made(shadow, from);
  CHECK(!t.stale(shadow, from));  // nothing changed: kept
  t.bump("lamps");                // something it isn't made from
  CHECK(!t.stale(shadow, from));
  t.bump("placements");           // something moved
  CHECK(t.stale(shadow, from));
  t.made(shadow, from);
  CHECK(!t.stale(shadow, from));
  CHECK(t.stale(shadow, {"placements", "sun"}));  // made from a different list: made again
}

TEST(change_tracker_set_bumps_only_a_different_value) {
  ChangeTracker t;
  ChangeTracker::Made m;
  float view[4] = {1, 2, 3, 4};
  t.set("sun view", view);
  t.made(m, {"sun view"});
  t.set("sun view", view);  // worked out again, the same: kept
  CHECK(!t.stale(m, {"sun view"}));
  view[2] = 3.5f;
  t.set("sun view", view);
  CHECK(t.stale(m, {"sun view"}));
  CHECK(t.version("sun view") == 3 && t.version("never seen") == 0);
}
