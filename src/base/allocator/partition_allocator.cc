// Ported from: blink/renderer/platform/wtf/allocator/partition_allocator.cc

// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "base/allocator/partition_allocator.h"

namespace bkfont {

void* PartitionAllocator::AllocateBacking(std::size_t size,
                                          const char* type_name) {
  return Partitions::BufferMalloc(size, type_name);
}

void PartitionAllocator::FreeBacking(void* address) {
  Partitions::BufferFree(address);
}

template <>
char* PartitionAllocator::AllocateVectorBacking<char>(std::size_t size) {
  return reinterpret_cast<char*>(
      AllocateBacking(size, "PartitionAllocator::allocateVectorBacking<char>"));
}

} // namespace bkfont
