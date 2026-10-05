// Ported from: skia/src/core/SkMask.cpp

#include "mask.h"

#include <limits>

namespace bkfont {

namespace {

// Returns the product if it is positive and fits in 31 bits. Otherwise this
// returns 0.
std::int32_t SafeMul32(std::int32_t a, std::int32_t b) {
  const std::int64_t size = static_cast<std::int64_t>(a) * b;
  if (size > 0 && size <= std::numeric_limits<std::int32_t>::max()) {
    return static_cast<std::int32_t>(size);
  }
  return 0;
}

} // namespace

std::size_t Mask::ComputeImageSize() const {
  // fRowBytes converts to the int32_t parameter as upstream.
  return static_cast<std::size_t>(SafeMul32(bounds.Height(), static_cast<std::int32_t>(row_bytes)));
}

std::size_t Mask::ComputeTotalImageSize() const {
  std::size_t size = ComputeImageSize();
  if (format == MaskFormat::k3D) {
    size = static_cast<std::size_t>(SafeMul32(static_cast<std::int32_t>(size), 3));
  }
  return size;
}

} // namespace bkfont
