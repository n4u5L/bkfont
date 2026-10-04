#pragma once
#include <memory>
namespace bkfont::base {
template <typename T>
std::unique_ptr<T> WrapUnique(T* value) {
  return std::unique_ptr<T>(value);
}
} // namespace bkfont::base
