#pragma once
#include <memory>
namespace base {
template <typename T>
std::unique_ptr<T> WrapUnique(T* value) {
  return std::unique_ptr<T>(value);
}
} // namespace base
