// Ported from: skia/include/private/base/SkOnce.h

#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

namespace bkit {

class Once {
public:
  constexpr Once() = default;
  constexpr ~Once() = default;

  template <typename Fn, typename... Args>
  void operator()(Fn&& fn, Args&&... args) {
    std::uint8_t state = state_.load(std::memory_order_acquire);
    if (state == kDone) {
      return;
    }
    if (state == kNotStarted &&
        state_.compare_exchange_strong(state, kClaimed,
                                       std::memory_order_relaxed, std::memory_order_relaxed)) {
      fn(std::forward<Args>(args)...);
      return state_.store(kDone, std::memory_order_release);
    }

    while (state_.load(std::memory_order_acquire) != kDone) {
    }
  }

private:
  enum State : std::uint8_t {
    kNotStarted,
    kClaimed,
    kDone
  };
  std::atomic<std::uint8_t> state_{kNotStarted};
};

} // namespace bkit
