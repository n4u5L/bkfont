// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Source: platform/wtf/allocator/partitions.cc. CRT allocation is the selected
// native boundary. Chromium's partitions, heap profiling and OOM crash reporting
// are not required here; allocation failure uses the C++ allocation exception.
#include "base/allocator/partitions.h"

#include <cstdlib>
#include <new>

namespace blink {

void Partitions::Initialize() {
  // The CRT heap needs no explicit initialization.
}

std::size_t Partitions::ComputeAllocationSize(std::size_t count,
                                              std::size_t element_size) {
  if (element_size && count > kMaxBackingSize / element_size)
    throw std::bad_array_new_length();
  return count * element_size;
}

void* Partitions::BufferTryMalloc(std::size_t size, const char*) {
  if (size > kMaxBackingSize)
    return nullptr;
  // As in PartitionAlloc, even a zero-size allocation has a distinct, freeable
  // address on success. Do not depend on the CRT's malloc(0) convention.
  return std::malloc(size ? size : 1);
}

void* Partitions::BufferMalloc(std::size_t size, const char* type_name) {
  if (void* result = BufferTryMalloc(size, type_name))
    return result;
  throw std::bad_alloc();
}

void* Partitions::BufferTryRealloc(void* address,
                                   std::size_t size,
                                   const char* type_name) {
  // Preserve PartitionRoot::ReallocInline's order for null and zero inputs.
  if (!address)
    return BufferTryMalloc(size, type_name);
  if (!size) {
    BufferFree(address);
    return nullptr;
  }
  if (size > kMaxBackingSize)
    return nullptr;
  // On failure the original allocation remains owned by the caller.
  return std::realloc(address, size);
}

void* Partitions::BufferRealloc(void* address,
                                std::size_t size,
                                const char* type_name) {
  void* result = BufferTryRealloc(address, size, type_name);
  if (result || (address && !size))
    return result;
  throw std::bad_alloc();
}

void Partitions::BufferFree(void* address) {
  std::free(address);
}

std::size_t Partitions::BufferPotentialCapacity(std::size_t size) {
  if (size > kMaxBackingSize)
    throw std::bad_array_new_length();
  return size;
}

std::size_t Partitions::BufferActualSize(std::size_t size) {
  return BufferPotentialCapacity(size);
}

void* Partitions::FastMalloc(std::size_t size, const char* type_name) {
  return BufferMalloc(size, type_name);
}

void* Partitions::FastZeroedMalloc(std::size_t size, const char*) {
  if (size <= kMaxBackingSize) {
    if (void* result = std::calloc(size ? size : 1, 1))
      return result;
  }
  throw std::bad_alloc();
}

void Partitions::FastFree(void* address) {
  std::free(address);
}

} // namespace blink
