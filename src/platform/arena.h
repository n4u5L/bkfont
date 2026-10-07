// Local implementation: owner of strike-lifetime allocations, standing in for
// skia/src/base/SkArenaAlloc.h. Objects and byte blocks keep their addresses
// until the arena is destroyed; objects are destroyed in reverse order of
// creation, as SkArenaAlloc does.

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace bkit {

class Arena {
public:
  Arena() = default;
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  ~Arena() {
    while (!objects_.empty()) {
      objects_.pop_back();
    }
  }

  template <typename T, typename... Args>
  T* Make(Args&&... args) {
    auto holder = std::make_unique<Holder<T>>(std::forward<Args>(args)...);
    T* object = &holder->value;
    objects_.push_back(std::move(holder));
    return object;
  }

  // Uninitialized bytes, aligned for any glyph image format.
  std::byte* MakeBytes(std::size_t size) {
    blocks_.push_back(std::make_unique_for_overwrite<std::byte[]>(size));
    return blocks_.back().get();
  }

private:
  struct HolderBase {
    virtual ~HolderBase() = default;
  };
  template <typename T>
  struct Holder final : HolderBase {
    template <typename... Args>
    explicit Holder(Args&&... args)
        : value(std::forward<Args>(args)...) {
    }
    T value;
  };

  std::vector<std::unique_ptr<HolderBase>> objects_;
  std::vector<std::unique_ptr<std::byte[]>> blocks_;
};

} // namespace bkit
