// Source: platform/wtf/allocator/partitions.h.
// The font port uses CRT storage instead of Chromium's process-wide partitions.
// Requested capacities are exact; no PartitionAlloc bucket rounding is applied.
#pragma once
#include <cstddef>
namespace blink {
class Partitions {
public:
  // Original MaxDirectMapped(): (1 << 31) - kSuperPageSize (1 << 21).
  // Keep the backing-size limit used by WTF's 32-bit sizes.
  static constexpr std::size_t kMaxBackingSize =
      (std::size_t{1} << 31) - (std::size_t{1} << 21);
  static void Initialize();
  static void* BufferMalloc(std::size_t, const char*);
  static void* BufferTryMalloc(std::size_t, const char*);
  static void* BufferRealloc(void*, std::size_t, const char*);
  static void* BufferTryRealloc(void*, std::size_t, const char*);
  static void BufferFree(void*);
  static std::size_t BufferActualSize(std::size_t);
  static std::size_t BufferPotentialCapacity(std::size_t);
  static std::size_t ComputeAllocationSize(std::size_t, std::size_t);
  static void* FastMalloc(std::size_t, const char*);
  static void* FastZeroedMalloc(std::size_t, const char*);
  static void FastFree(void*);
};
} // namespace blink
