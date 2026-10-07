// Ported from: chromium/base/threading/thread_local_storage.h
// Ported from: chromium/base/threading/thread_local_storage.cc
// Ported from: chromium/base/threading/thread_local_storage_win.cc

#pragma once
#include "build/build_config.h"
#if BUILDFLAG(IS_POSIX)
#include "base/threading/thread_local_storage_posix.h"
#else
#include <cstddef>
#include <cstdint>

namespace bkit::base {

class ThreadLocalStorage {
public:
  class Slot {
  public:
    using Destructor = void (*)(void*);
    explicit Slot(Destructor destructor);
    ~Slot();
    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
    void* Get() const;
    void Set(void* value);

  private:
    std::size_t slot_ = 256;
    std::uint32_t version_ = 0;
  };
};

} // namespace bkit::base

#endif
