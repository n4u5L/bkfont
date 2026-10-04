#pragma once
#include <memory>
namespace bkfont {
template <class T>
bool FontValuesEquivalent(const std::shared_ptr<T>& a, const std::shared_ptr<T>& b) {
  return a == b || (a && b && *a == *b);
}
} // namespace bkfont
