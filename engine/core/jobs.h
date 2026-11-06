#pragma once
// A thread pool and parallel-for. On the web (no threads) everything runs
// inline on the calling thread.
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace loop {

class JobSystem {
 public:
  explicit JobSystem(int threads = 0);  // 0 = hardware threads
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;
  int threads() const { return static_cast<int>(workers_.size()) + 1; }
  // Runs fn(i) for i in [0, count), spread over every thread (the caller
  // helps); returns when all are done. `grain` items are taken at a time.
  // A second caller while one is running (the background cook while the
  // frame builds a sky) runs its items itself, on its own thread.
  void parallelFor(int count, const std::function<void(int)>& fn, int grain = 1);
  static JobSystem& shared();

 private:
  void workerLoop();
  std::vector<std::thread> workers_;
  std::mutex m_, caller_;
  std::condition_variable cv_, doneCv_;
  const std::function<void(int)>* fn_ = nullptr;
  std::atomic<int> next_{0};
  int count_ = 0, grain_ = 1, busy_ = 0;
  uint64_t generation_ = 0;
  bool quit_ = false;
};

}  // namespace loop
