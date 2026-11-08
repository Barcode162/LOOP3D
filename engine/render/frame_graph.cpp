#include "engine/render/frame_graph.h"

#include <algorithm>

namespace loop {

namespace {

std::string joined(const std::vector<std::string>& v) {
  std::string s;
  for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
  return s;
}

}  // namespace

FrameGraph::Step& FrameGraph::add(std::string name, std::vector<std::string> reads, std::vector<std::string> writes, std::function<void()> run, bool on) {
  steps_.push_back({std::move(name), std::move(reads), std::move(writes), on, std::move(run)});
  return steps_.back();
}

std::vector<std::string> FrameGraph::check() const {
  std::vector<std::string> faults, written = outside_, names;
  for (const Step& s : steps_) {
    if (std::find(names.begin(), names.end(), s.name) != names.end()) faults.push_back("two steps are called \"" + s.name + "\"");
    names.push_back(s.name);
    for (const std::string& r : s.reads)
      if (std::find(written.begin(), written.end(), r) == written.end())
        faults.push_back("\"" + s.name + "\" reads " + r + ", which no step before it writes and which doesn't come from outside the frame");
    if (s.writes.empty()) faults.push_back("\"" + s.name + "\" writes nothing");
    for (const std::string& w : s.writes) written.push_back(w);
  }
  return faults;
}

std::vector<std::string> FrameGraph::describe() const {
  std::vector<std::string> out;
  for (const Step& s : steps_) out.push_back(s.name + ": reads " + (s.reads.empty() ? "nothing" : joined(s.reads)) + "; writes " + joined(s.writes));
  return out;
}

void FrameGraph::run() {
  for (size_t i = 0; i < steps_.size(); ++i) {
    if (!steps_[i].on || !steps_[i].run) continue;
    current_ = static_cast<int>(i);
    steps_[i].run();
  }
  current_ = -1;
}

void FrameGraph::addTimes(const std::vector<Stamp>& stamps) {
  // this frame's total per step (a step can stamp several passes)
  std::vector<std::pair<std::string, double>> frame;
  for (const Stamp& s : stamps) {
    if (s.end < s.begin) continue;  // a timer that went backwards says nothing
    double ms = static_cast<double>(s.end - s.begin) / 1e6;
    auto it = std::find_if(frame.begin(), frame.end(), [&](auto& p) { return p.first == s.step; });
    if (it == frame.end()) frame.push_back({s.step, ms});
    else it->second += ms;
  }
  for (Time& t : times_) ++t.missed;
  for (auto& [step, ms] : frame) {
    auto it = std::find_if(times_.begin(), times_.end(), [&](const Time& t) { return t.step == step; });
    if (it == times_.end()) {
      times_.push_back({step, static_cast<float>(ms), 0, 0});
      it = times_.end() - 1;
    } else {
      it->ms += (static_cast<float>(ms) - it->ms) * 0.1f;
    }
    it->sumMs += ms, ++it->frames, it->missed = 0;
  }
}

std::vector<FrameGraph::Time> FrameGraph::recent() const {
  std::vector<Time> out;
  for (const Time& t : times_)
    if (t.missed < kGone) out.push_back(t);
  return out;
}

std::vector<std::pair<std::string, float>> FrameGraph::takeAverages() {
  std::vector<std::pair<std::string, float>> out;
  for (Time& t : times_) {
    if (t.frames) out.push_back({t.step, static_cast<float>(t.sumMs / t.frames)});
    t.sumMs = 0, t.frames = 0;
  }
  // a step gone for good (and already counted) is forgotten
  times_.erase(std::remove_if(times_.begin(), times_.end(), [](const Time& t) { return t.missed >= kGone && !t.frames; }), times_.end());
  return out;
}

std::vector<std::string> FrameGraph::storageFaults(const std::string& pipeline, const std::vector<StageBuffers>& groups, int limit) {
  StageBuffers sum;
  for (const StageBuffers& g : groups) sum.vertex += g.vertex, sum.fragment += g.fragment, sum.compute += g.compute;
  std::vector<std::string> faults;
  for (auto [stage, n] : {std::pair<const char*, int>{"vertex", sum.vertex}, {"fragment", sum.fragment}, {"compute", sum.compute}})
    if (n > limit)
      faults.push_back("the " + pipeline + " pipeline's " + stage + " stage reads " + std::to_string(n) + " storage buffers; the iPhone allows " +
                       std::to_string(limit));
  return faults;
}

}  // namespace loop
