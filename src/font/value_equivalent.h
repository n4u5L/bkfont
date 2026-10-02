#pragma once
#include <memory>
namespace blink {
template <class T>
bool FontValuesEquivalent(const std::shared_ptr<T>& a, const std::shared_ptr<T>& b) {
  return a == b || (a && b && *a == *b);
}
} // namespace blink
