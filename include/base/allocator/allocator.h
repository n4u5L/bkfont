// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Source: platform/wtf/allocator/allocator.h; non-GC allocation facilities.
#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include "base/compiler_specific.h"
#include "base/allocator/partitions.h"
#include "base/ref_counted.h"
#include "base/type_traits.h"
namespace base {
enum class NotNullTag {
  kNotNull
};
} // namespace base
inline void* operator new(std::size_t, base::NotNullTag, void* address) noexcept {
  return address;
}
inline void operator delete(void*, base::NotNullTag, void*) noexcept {
}
namespace blink {
class PartitionAllocator;
namespace internal {
class __thisIsHereToForceASemicolonAfterThisMacro;
} // namespace internal
} // namespace blink

#define DISALLOW_NEW()                                                \
public:                                                               \
  using IsDisallowNewMarker [[maybe_unused]] = int;                   \
  void* operator new(std::size_t, base::NotNullTag, void* location) { \
    return location;                                                  \
  }                                                                   \
  void* operator new(std::size_t, void* location) {                   \
    return location;                                                  \
  }                                                                   \
                                                                      \
private:                                                              \
  void* operator new(std::size_t) = delete;                           \
                                                                      \
public:                                                               \
  friend class ::blink::internal::__thisIsHereToForceASemicolonAfterThisMacro

#define STATIC_ONLY(Type)                                            \
  Type() = delete;                                                   \
  Type(const Type&) = delete;                                        \
  Type& operator=(const Type&) = delete;                             \
  void* operator new(std::size_t) = delete;                          \
  void* operator new(std::size_t, base::NotNullTag, void*) = delete; \
  void* operator new(std::size_t, void*) = delete

#if defined(OFFICIAL_BUILD)
#define BASE_HEAP_PROFILER_TYPE_NAME(T) nullptr
#else
#define BASE_HEAP_PROFILER_TYPE_NAME(T) ::blink::GetStringWithTypeName<T>()
#endif

#define USING_FAST_MALLOC(type) \
  USING_FAST_MALLOC_INTERNAL(type, BASE_HEAP_PROFILER_TYPE_NAME(type))
#define USING_FAST_MALLOC_WITH_TYPE_NAME(type) \
  USING_FAST_MALLOC_INTERNAL(type, #type)
#define USING_FAST_MALLOC_INTERNAL(type, type_name)                   \
public:                                                               \
  void* operator new(std::size_t, void* p) {                          \
    return p;                                                         \
  }                                                                   \
  void* operator new[](std::size_t, void* p) {                        \
    return p;                                                         \
  }                                                                   \
  void* operator new(std::size_t size) {                              \
    return ::blink::Partitions::FastMalloc(size, type_name);          \
  }                                                                   \
  void operator delete(void* p) {                                     \
    ::blink::Partitions::FastFree(p);                                 \
  }                                                                   \
  void* operator new[](std::size_t size) {                            \
    return ::blink::Partitions::FastMalloc(size, type_name);          \
  }                                                                   \
  void operator delete[](void* p) {                                   \
    ::blink::Partitions::FastFree(p);                                 \
  }                                                                   \
  void* operator new(std::size_t, base::NotNullTag, void* location) { \
    return location;                                                  \
  }                                                                   \
  void operator delete(void*, base::NotNullTag, void*) {}             \
  void operator delete(void*, void*) {}                               \
  void operator delete[](void*, void*) {}                             \
                                                                      \
private:                                                              \
  friend class ::blink::internal::__thisIsHereToForceASemicolonAfterThisMacro
