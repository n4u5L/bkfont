// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Source: platform/wtf/allocator/partition_allocator.h. Non-GC backing-store
// policy, with Partitions supplied by the CRT boundary in this port.
#pragma once

#include <cstddef>
#include <cstring>

#include "base/allocator/allocator.h"

namespace blink {

class PartitionAllocator {
public:
  template <typename T>
  static std::size_t MaxElementCountInBackingStore() {
    return Partitions::kMaxBackingSize / sizeof(T);
  }

  template <typename T>
  static std::size_t QuantizedSize(std::size_t count) {
    return Partitions::BufferPotentialCapacity(
        Partitions::ComputeAllocationSize(count, sizeof(T)));
  }

  template <typename T>
  static T* AllocateVectorBacking(std::size_t size) {
    return reinterpret_cast<T*>(
        AllocateBacking(size, BASE_HEAP_PROFILER_TYPE_NAME(T)));
  }

  static void FreeVectorBacking(void* address) {
    FreeBacking(address);
  }
  static bool ExpandVectorBacking(void*, std::size_t) {
    return false;
  }
  static bool ShrinkVectorBacking(void*,
                                  std::size_t quantized_current_size,
                                  std::size_t quantized_shrunk_size) {
    return quantized_current_size == quantized_shrunk_size;
  }

  template <typename T, typename HashTable>
  static T* AllocateHashTableBacking(std::size_t size) {
    return reinterpret_cast<T*>(
        AllocateBacking(size, BASE_HEAP_PROFILER_TYPE_NAME(T)));
  }

  template <typename T, typename HashTable>
  static T* AllocateZeroedHashTableBacking(std::size_t size) {
    void* result = AllocateBacking(size, BASE_HEAP_PROFILER_TYPE_NAME(T));
    std::memset(result, 0, size);
    return reinterpret_cast<T*>(result);
  }

  template <typename T, typename HashTable>
  static void FreeHashTableBacking(void* address) {
    FreeBacking(address);
  }

  template <typename Return, typename Metadata>
  static Return Malloc(std::size_t size, const char* type_name) {
    return reinterpret_cast<Return>(Partitions::FastMalloc(size, type_name));
  }

  template <typename T, typename HashTable>
  static bool ExpandHashTableBacking(void*, std::size_t) {
    return false;
  }
  template <typename Traits>
  static bool CanReuseHashTableDeletedBucket() {
    return true;
  }

  static void Free(void* address) {
    Partitions::FastFree(address);
  }
  template <typename T>
  static void* NewArray(std::size_t bytes) {
    return Malloc<void*, void>(bytes, BASE_HEAP_PROFILER_TYPE_NAME(T));
  }
  static void DeleteArray(void* address) {
    Free(address);
  }

private:
  static void* AllocateBacking(std::size_t size, const char* type_name);
  static void FreeBacking(void* address);
};

template <>
char* PartitionAllocator::AllocateVectorBacking<char>(std::size_t size);

} // namespace blink

#define USE_ALLOCATOR(ClassName, Allocator)                           \
public:                                                               \
  void* operator new(std::size_t size) {                              \
    return Allocator::template Malloc<void*, ClassName>(              \
        size,                                                         \
        BASE_HEAP_PROFILER_TYPE_NAME(ClassName));                     \
  }                                                                   \
  void operator delete(void* p) {                                     \
    Allocator::Free(p);                                               \
  }                                                                   \
  void* operator new[](std::size_t size) {                            \
    return Allocator::template NewArray<ClassName>(size);             \
  }                                                                   \
  void operator delete[](void* p) {                                   \
    Allocator::DeleteArray(p);                                        \
  }                                                                   \
  void* operator new(std::size_t, base::NotNullTag, void* location) { \
    return location;                                                  \
  }                                                                   \
  void* operator new(std::size_t, void* location) {                   \
    return location;                                                  \
  }                                                                   \
  void operator delete(void*, base::NotNullTag, void*) {}             \
  void operator delete(void*, void*) {}                               \
                                                                      \
private:                                                              \
  typedef int __thisIsHereToForceASemicolonAfterThisMacro
