// Ported from: skia/src/core/SkGlyph.h, skia/src/core/SkMask.h

#pragma once

#include <cstdint>

#include "rect.h"

namespace bkfont {

// SkMask::Format.
enum class MaskFormat : std::uint8_t {
  kBW,
  kA8,
  k3D,
  kARGB32,
  kLCD16,
  kSDF
};

// SkGlyph, metrics only. Images, paths and drawables are not ported. The
// glyph id carries no subpixel offset, as SkPackedGlyphID{glyphID} on the
// metrics path.
class PlatformGlyph {
public:
  explicit PlatformGlyph(std::uint16_t glyph_id)
      : glyph_id_(glyph_id) {
  }

  std::uint16_t GetGlyphID() const {
    return glyph_id_;
  }
  float AdvanceX() const {
    return advance_x_;
  }
  float AdvanceY() const {
    return advance_y_;
  }
  MaskFormat GetMaskFormat() const {
    return mask_format_;
  }
  std::uint16_t ExtraBits() const {
    return scaler_context_bits_;
  }
  int Left() const {
    return left_;
  }
  int Top() const {
    return top_;
  }
  int Width() const {
    return width_;
  }
  int Height() const {
    return height_;
  }
  ScalarRect Rect() const {
    return ScalarRect::MakeXYWH(left_, top_, width_, height_);
  }

private:
  friend class ScalerContext;

  float advance_x_ = 0;
  float advance_y_ = 0;
  std::uint16_t width_ = 0;
  std::uint16_t height_ = 0;
  std::int16_t top_ = 0;
  std::int16_t left_ = 0;
  std::uint16_t glyph_id_;
  MaskFormat mask_format_ = MaskFormat::kBW;
  std::uint16_t scaler_context_bits_ = 0;
};

} // namespace bkfont
