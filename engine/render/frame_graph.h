#pragma once
// The frame graph (the picture pipeline's phase 2, docs/PIPELINE.md): a frame
// as named steps, each saying what it reads and writes, run in order. It
// keeps no pictures itself; the live renderer (engine/gpu/renderer.cpp)
// builds its frame from it. What it gives:
//   - the frame's shape in words: which steps, in which order, reading and
//     writing what (check() finds a step reading something no earlier step
//     writes and that doesn't come from outside the frame);
//   - each step's time: the renderer stamps every pass of the graphics chip
//     with the step it belongs to (the chip's own timers, where it has them),
//     and the graph adds the stamps up per step, smoothed for the meter and
//     summed for the recordings.
// It knows nothing of WebGPU, so it is tested on its own (tests/frame_graph_test.cpp).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace loop {

class FrameGraph {
 public:
  struct Step {
    std::string name;
    std::vector<std::string> reads, writes;
    bool on = true;
    std::function<void()> run;
  };

  // What the frame finds already there: the scene, last frame's pictures …
  explicit FrameGraph(std::vector<std::string> outside = {}) : outside_(std::move(outside)) {}

  // A new frame's steps, added in the order they run.
  void clear() { steps_.clear(), current_ = -1; }
  Step& add(std::string name, std::vector<std::string> reads, std::vector<std::string> writes, std::function<void()> run, bool on = true);
  const std::vector<Step>& steps() const { return steps_; }

  // The order's faults, in plain words (empty: none). Steps that are off
  // count as if on: the order must hold whichever steps run.
  std::vector<std::string> check() const;
  // The frame's shape in one line each: "shadow: reads scene, sun list; writes shadow map".
  std::vector<std::string> describe() const;

  // Runs each step that is on, in order. While one runs, current() is its
  // place in steps() (-1 between steps).
  void run();
  int current() const { return current_; }

  // ---- time, from the graphics chip's timers
  // One frame's stamps, in the order the passes were sent: for each, the step
  // it belonged to (by name) and its start and end ticks (nanoseconds). Adds
  // each step's share of the frame: how far its passes moved the frame's
  // finish line (the steps add up to the whole frame, overlapping or not).
  struct Stamp {
    std::string step;
    uint64_t begin = 0, end = 0;
  };
  void addTimes(const std::vector<Stamp>& stamps);
  struct Time {
    std::string step;
    float ms = 0;          // smoothed, for the meter
    double sumMs = 0;      // added up since takeAverages(), for the recordings
    uint32_t frames = 0;
    uint32_t missed = 0;   // timed frames since this step last ran
  };
  const std::vector<Time>& times() const { return times_; }
  // The steps running now: a step that hasn't run for a second of timed
  // frames (the old drawing, once a scene's clusters are cooked) drops out.
  static constexpr uint32_t kGone = 60;
  std::vector<Time> recent() const;
  // Each step's average over the frames since the last call, then starts again.
  std::vector<std::pair<std::string, float>> takeAverages();
  bool timed() const { return !times_.empty(); }

  // ---- the iPhone's limit: storage buffers a shader stage may read
  // What one bind group gives each stage; a pipeline's groups add up, and a
  // stage over `limit` (8 on the iPhone, though Safari reports more) is a
  // fault in words, naming the pipeline (the step it serves) and the stage.
  struct StageBuffers {
    int vertex = 0, fragment = 0, compute = 0;
  };
  static constexpr int kStorageBufferLimit = 8;
  static std::vector<std::string> storageFaults(const std::string& pipeline, const std::vector<StageBuffers>& groups,
                                                int limit = kStorageBufferLimit);

 private:
  std::vector<std::string> outside_;
  std::vector<Step> steps_;
  int current_ = -1;
  std::vector<Time> times_;
  uint64_t lastEnd_ = 0;  // where the last timed frame finished
};

}  // namespace loop
