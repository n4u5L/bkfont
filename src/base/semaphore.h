// Ported from: skia/include/private/base/SkSemaphore.h

#pragma once

#include <algorithm>
#include <atomic>

#include "once.h"

namespace bkit {

class Semaphore {
public:
  constexpr Semaphore(int count = 0)
      : count_(count),
        os_semaphore_(nullptr) {
  }
  ~Semaphore();
  Semaphore(const Semaphore&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;

  void Signal(int n = 1);
  void Wait();
  bool TryWait();

private:
  // This implementation follows the general strategy of
  //     'A Lightweight Semaphore with Partial Spinning'
  // found here
  //     http://preshing.com/20150316/semaphores-are-surprisingly-versatile/
  //
  // We wrap an OS-provided semaphore with a user-space atomic counter that
  // lets us avoid interacting with the OS semaphore unless strictly required:
  // moving the count from >=0 to <0 or vice-versa, i.e. sleeping or waking
  // threads.
  struct OSSemaphore;

  void OsSignal(int n);
  void OsWait();

  std::atomic<int> count_;
  Once os_semaphore_once_;
  OSSemaphore* os_semaphore_;
};

inline void Semaphore::Signal(int n) {
  const int prev = count_.fetch_add(n, std::memory_order_release);

  const int to_signal = std::min(-prev, n);
  if (to_signal > 0) {
    this->OsSignal(to_signal);
  }
}

inline void Semaphore::Wait() {
  if (count_.fetch_sub(1, std::memory_order_acquire) <= 0) {
    this->OsWait();
  }
}

} // namespace bkit
