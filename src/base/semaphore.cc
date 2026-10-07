// Ported from: skia/src/base/SkSemaphore.cpp

#include "semaphore.h"

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#elif defined(_WIN32)
#include "lean_windows.h"
#else
// It's important we test for Mach before this. This code will compile but not
// work there.
#include <errno.h>
#include <semaphore.h>
#endif

namespace bkit {

#ifdef __APPLE__
struct Semaphore::OSSemaphore {
  dispatch_semaphore_t semaphore;

  OSSemaphore() {
    semaphore = dispatch_semaphore_create(0);
  }
  ~OSSemaphore() {
    dispatch_release(semaphore);
  }

  void Signal(int n) {
    while (n-- > 0) {
      dispatch_semaphore_signal(semaphore);
    }
  }
  void Wait() {
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
  }
};
#elif defined(_WIN32)
struct Semaphore::OSSemaphore {
  HANDLE semaphore;

  OSSemaphore() {
    semaphore = CreateSemaphore(nullptr, 0, MAXLONG, nullptr);
  }
  ~OSSemaphore() {
    CloseHandle(semaphore);
  }

  void Signal(int n) {
    ReleaseSemaphore(semaphore, n, nullptr);
  }
  void Wait() {
    WaitForSingleObject(semaphore, INFINITE);
  }
};
#else
struct Semaphore::OSSemaphore {
  sem_t semaphore;

  OSSemaphore() {
    sem_init(&semaphore, 0, 0);
  }
  ~OSSemaphore() {
    sem_destroy(&semaphore);
  }

  void Signal(int n) {
    while (n-- > 0) {
      sem_post(&semaphore);
    }
  }
  void Wait() {
    while (sem_wait(&semaphore) == -1 && errno == EINTR) {
    }
  }
};
#endif

Semaphore::~Semaphore() {
  delete os_semaphore_;
}

void Semaphore::OsSignal(int n) {
  os_semaphore_once_([this] { os_semaphore_ = new OSSemaphore; });
  os_semaphore_->Signal(n);
}

void Semaphore::OsWait() {
  os_semaphore_once_([this] { os_semaphore_ = new OSSemaphore; });
  os_semaphore_->Wait();
}

bool Semaphore::TryWait() {
  int count = count_.load(std::memory_order_relaxed);
  if (count > 0) {
    return count_.compare_exchange_weak(count, count - 1, std::memory_order_acquire);
  }
  return false;
}

} // namespace bkit
