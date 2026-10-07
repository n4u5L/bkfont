// Local implementation: Chromium lock API adapter using the C++ standard library.
// Upstream reference: chromium/base/synchronization/lock.h
// AtomicStringTable retains its upstream single shared lock and weak entries.
#pragma once
#include <mutex>
namespace bkit::base {

class Lock {
public:
  void Acquire() {
    mutex_.lock();
  }
  void Release() {
    mutex_.unlock();
  }

private:
  std::mutex mutex_;
};
class AutoLock {
public:
  explicit AutoLock(Lock& lock)
      : lock_(lock) {
    lock_.Acquire();
  }
  ~AutoLock() {
    lock_.Release();
  }
  AutoLock(const AutoLock&) = delete;
  AutoLock& operator=(const AutoLock&) = delete;

private:
  Lock& lock_;
};

} // namespace bkit::base

#define GUARDED_BY(...)
#define EXCLUSIVE_LOCKS_REQUIRED(...)
#define LOCKS_EXCLUDED(...)
