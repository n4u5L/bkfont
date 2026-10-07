// Ported from: chromium/base/memory/values_equivalent.h
#pragma once
namespace bkit::base {

template <typename T, typename U>
bool ValuesEquivalent(const T& first, const U& second) {
  return first == second || (first && second && *first == *second);
}

} // namespace bkit::base
