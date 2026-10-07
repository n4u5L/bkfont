// Ported from: chromium/base/memory/ptr_util.h
#pragma once
#include <memory>
namespace bkit::base {

template <typename T>
std::unique_ptr<T> WrapUnique(T* value) {
  return std::unique_ptr<T>(value);
}

} // namespace bkit::base
