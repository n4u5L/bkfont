// Source: base/memory/values_equivalent.h.
#pragma once
namespace bkfont::base {
template <typename T, typename U>
bool ValuesEquivalent(const T& first, const U& second) {
  return first == second || (first && second && *first == *second);
}
} // namespace bkfont::base
