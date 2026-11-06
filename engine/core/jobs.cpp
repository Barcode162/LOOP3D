#include "engine/core/jobs.h"

#include <algorithm>

#include "engine/core/settings.h"

namespace loop {

JobSystem::JobSystem(int threads) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  (void)threads;
#else
  if (threads <= 0) threads = static_cast<int>(std::thread::hardware_concurrency());
#if defined(__EMSCRIPTEN_PTHREADS__)
  // the web player's workers are started with the page (PTHREAD_POOL_SIZE in
  // CMakeLists.txt: one fewer than the cores, at most 7): never ask for more,
  // or the page would wait for a worker that can't start
  threads = std::clamp(threads, 1, 8);
#endif
  for (int i = 1; i < threads; ++i) workers_.emplace_back([this] { workerLoop(); });
#endif
}

JobSystem::~JobSystem() {
  {
    std::lock_guard<std::mutex> l(m_);
    quit_ = true;
  }
  cv_.notify_all();
  for (auto& t : workers_) t.join();
}

JobSystem& JobSystem::shared() {
  static JobSystem js(config().cinema.threads);  // 0 = every hardware thread
  return js;
}

void JobSystem::workerLoop() {
  uint64_t seen = 0;
  for (;;) {
    const std::function<void(int)>* fn;
    {
      std::unique_lock<std::mutex> l(m_);
      cv_.wait(l, [&] { return quit_ || generation_ != seen; });
      if (quit_) return;
      seen = generation_;
      fn = fn_;
      ++busy_;
    }
    while (fn) {  // null: this round already finished without us
      int i = next_.fetch_add(grain_);
      if (i >= count_) break;
      int end = std::min(count_, i + grain_);
      for (int k = i; k < end; ++k) (*fn)(k);
    }
    {
      std::lock_guard<std::mutex> l(m_);
      --busy_;
    }
    doneCv_.notify_all();
  }
}

void JobSystem::parallelFor(int count, const std::function<void(int)>& fn, int grain) {
  if (count <= 0) return;
  std::unique_lock<std::mutex> mine(caller_, std::try_to_lock);  // the workers are someone else's now: alone, then
  if (workers_.empty() || count <= grain || !mine.owns_lock()) {
    for (int i = 0; i < count; ++i) fn(i);
    return;
  }
  {
    std::lock_guard<std::mutex> l(m_);
    fn_ = &fn;
    count_ = count;
    grain_ = std::max(1, grain);
    next_ = 0;
    ++generation_;
  }
  cv_.notify_all();
  for (;;) {
    int i = next_.fetch_add(grain_);
    if (i >= count_) break;
    int end = std::min(count_, i + grain_);
    for (int k = i; k < end; ++k) fn(k);
  }
  std::unique_lock<std::mutex> l(m_);
  doneCv_.wait(l, [&] { return busy_ == 0 && next_ >= count_; });
  fn_ = nullptr;
}

}  // namespace loop
