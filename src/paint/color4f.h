// Ported from: skia/include/core/SkColor.h
// Ported from: skia/src/core/SkColor.cpp

#pragma once

#include <algorithm>
#include <cstdint>

#include "mask_gamma.h"

namespace bkfont {

// SkRGBA4f. Unpremultiplied (Color4f) or premultiplied (PMColor4f) floats in
// the sRGB-encoded legacy color space, which is the only one used here.
struct RGBA4f {
  float r = 0;
  float g = 0;
  float b = 0;
  float a = 0;

  bool operator==(const RGBA4f& other) const = default;

  RGBA4f operator*(float scale) const {
    return {r * scale, g * scale, b * scale, a * scale};
  }

  // SkRGBA4f::premul.
  RGBA4f Premul() const {
    return {r * a, g * a, b * a, a};
  }

  // SkRGBA4f::unpremul.
  RGBA4f Unpremul() const {
    if (a == 0.0f) {
      return {0, 0, 0, 0};
    }
    const float inv_alpha = 1 / a;
    return {r * inv_alpha, g * inv_alpha, b * inv_alpha, a};
  }

  bool IsOpaque() const {
    return a == 1.0f;
  }

  // SkColor4f::FromColor.
  static RGBA4f FromColor(ColorARGB color) {
    constexpr float kScale = 1 / 255.0f;
    return {static_cast<float>((color >> 16) & 0xFF) * kScale,
            static_cast<float>((color >> 8) & 0xFF) * kScale,
            static_cast<float>(color & 0xFF) * kScale,
            static_cast<float>((color >> 24) & 0xFF) * kScale};
  }

  // SkColor4f::toSkColor: pinned to [0, 1] and rounded.
  ColorARGB ToColor() const {
    const auto to_byte = [](float v) {
      return static_cast<unsigned>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f);
    };
    return (to_byte(a) << 24) | (to_byte(r) << 16) | (to_byte(g) << 8) | to_byte(b);
  }
};

// SkColor4f.
using Color4f = RGBA4f;
// SkPMColor4f.
using PMColor4f = RGBA4f;

// SkColors::kTransparent and SkColors::kBlack.
inline constexpr Color4f kTransparentColor4f{0, 0, 0, 0};
inline constexpr Color4f kBlackColor4f{0, 0, 0, 1};

// SK_ColorTRANSPARENT, SK_ColorBLACK.
inline constexpr ColorARGB kColorTransparent = 0x00000000u;
inline constexpr ColorARGB kColorBlack = 0xFF000000u;

} // namespace bkfont
