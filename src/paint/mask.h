// Ported from: skia/src/core/SkMask.h
// Ported from: skia/src/core/SkMask.cpp

#pragma once

#include <cstddef>
#include <cstdint>

#include "rect.h"

namespace bkfont {

// SkMask::Format.
enum class MaskFormat : std::uint8_t {
  kBW,     // 1bit per pixel mask (e.g. monochrome)
  kA8,     // 8bits per pixel mask (e.g. antialiasing)
  k3D,     // 3 8bit per pixl planes: alpha, mul, add
  kARGB32, // SkPMColor
  kLCD16,  // 565 alpha for r/g/b
  kSDF,    // 8bits representing signed distance field
};

// SkMask::kCountMaskFormats.
inline constexpr int kCountMaskFormats = static_cast<int>(MaskFormat::kSDF) + 1;

// SkMask. The image is not owned.
struct Mask {
  Mask(const std::uint8_t* img, const IntRect& bounds, std::uint32_t row_bytes, MaskFormat format)
      : image(img), bounds(bounds), row_bytes(row_bytes), format(format) {
  }

  const std::uint8_t* const image;
  const IntRect bounds;
  const std::uint32_t row_bytes;
  const MaskFormat format;

  bool IsEmpty() const {
    return bounds.IsEmpty();
  }

  // Return the byte size of the mask, assuming only 1 plane. Does not
  // account for k3D_Format. For that, use ComputeTotalImageSize(). If there
  // is an overflow of 32bits, then returns 0.
  std::size_t ComputeImageSize() const;

  // Return the byte size of the mask, taking into account any extra planes
  // (e.g. k3D_Format). If there is an overflow of 32bits, then returns 0.
  std::size_t ComputeTotalImageSize() const;
};

// SkMaskBuilder. A mask whose image and bounds may be written.
struct MaskBuilder {
  MaskBuilder(std::uint8_t* img, const IntRect& bounds, std::uint32_t row_bytes, MaskFormat format)
      : image(img), bounds(bounds), row_bytes(row_bytes), format(format) {
  }

  std::uint8_t* image;
  IntRect bounds;
  std::uint32_t row_bytes;
  MaskFormat format;
};

// SkPMColor with SK_R32_SHIFT 16, as Chromium's SkUserConfig.h sets: the
// bytes are B, G, R, A in memory on little-endian targets.
using PMColor = std::uint32_t;

inline constexpr int kA32Shift = 24;
inline constexpr int kR32Shift = 16;
inline constexpr int kG32Shift = 8;
inline constexpr int kB32Shift = 0;

// SkPackARGB32. The components must already be premultiplied.
inline constexpr PMColor PackARGB32(unsigned a, unsigned r, unsigned g, unsigned b) {
  return (a << kA32Shift) | (r << kR32Shift) | (g << kG32Shift) | (b << kB32Shift);
}

// SkPack888ToRGB16.
inline constexpr std::uint16_t Pack888ToRGB16(unsigned r, unsigned g, unsigned b) {
  return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

} // namespace bkfont
