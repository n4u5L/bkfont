// Local implementation: non-owning pointer adapter without Chromium instrumentation.
// Upstream reference: chromium/base/memory/raw_ptr.h
// Non-owning pointer; Chromium's instrumentation and backup-reference allocator
// are not part of this port. Owning edges use explicit reference ownership.
#pragma once
#include <cstddef>

namespace bkfont {

template <typename T>
class raw_ptr {
public:
  constexpr raw_ptr() = default;
  constexpr raw_ptr(std::nullptr_t)
      : value_(nullptr) {
  }
  constexpr raw_ptr(T* value)
      : value_(value) {
  }
  constexpr T* get() const {
    return value_;
  }
  constexpr T* ExtractAsDangling() {
    T* result = value_;
    value_ = nullptr;
    return result;
  }
  constexpr operator T*() const {
    return value_;
  }
  constexpr T* operator->() const {
    return value_;
  }
  constexpr T& operator*() const {
    return *value_;
  }
  constexpr raw_ptr& operator=(T* value) {
    value_ = value;
    return *this;
  }

private:
  T* value_ = nullptr;
};

} // namespace bkfont
