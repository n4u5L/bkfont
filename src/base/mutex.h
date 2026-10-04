// Ported from: skia/include/private/base/SkMutex.h

#pragma once

#include "semaphore.h"
#include "thread_id.h"

namespace bkfont {

class Mutex {
public:
  constexpr Mutex() = default;
  ~Mutex() = default;
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  void Acquire() {
    semaphore_.Wait();
    owner_ = GetThreadID();
  }

  void Release() {
    owner_ = kIllegalThreadID;
    semaphore_.Signal();
  }

private:
  Semaphore semaphore_{1};
  ThreadID owner_{kIllegalThreadID};
};

class AutoMutexExclusive {
public:
  AutoMutexExclusive(Mutex& mutex)
      : mutex_(mutex) {
    mutex_.Acquire();
  }
  ~AutoMutexExclusive() {
    mutex_.Release();
  }

  AutoMutexExclusive(const AutoMutexExclusive&) = delete;
  AutoMutexExclusive(AutoMutexExclusive&&) = delete;

  AutoMutexExclusive& operator=(const AutoMutexExclusive&) = delete;
  AutoMutexExclusive& operator=(AutoMutexExclusive&&) = delete;

private:
  Mutex& mutex_;
};

} // namespace bkfont
