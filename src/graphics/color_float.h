/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
// Scalar extraction of include/core/SkColor.h, src/core/SkColor.cpp,
// src/core/SkSwizzlePriv.h and src/base/SkVx.h, without Skia types.
#pragma once
#include <cstdint>

namespace blink {
struct ColorFloat4 {
  float fR, fG, fB, fA;

  constexpr ColorFloat4 operator*(float scale) const {
    return {fR * scale, fG * scale, fB * scale, fA * scale};
  }
  constexpr ColorFloat4 premul() const {
    return {fR * fA, fG * fA, fB * fA, fA};
  }
  ColorFloat4 unpremul() const {
    if (fA == 0.f) return {0, 0, 0, 0};
    const float inverse_alpha = 1.f / fA;
    return {fR * inverse_alpha, fG * inverse_alpha, fB * inverse_alpha, fA};
  }
  std::uint32_t ToARGB32() const {
    // Sk4f_toL32(swizzle_rb(rgba)): round before clipping, and preserve
    // skvx::pin's comparison order (NaN maps to the lower bound, zero).
    const auto channel = [](float value) -> std::uint32_t {
      const float rounded = value * 255.f + 0.5f;
      const float upper_clipped = 255.f < rounded ? 255.f : rounded;
      const float clipped = 0.f < upper_clipped ? upper_clipped : 0.f;
      return static_cast<std::uint8_t>(clipped);
    };
    return channel(fA) << 24 | channel(fR) << 16 | channel(fG) << 8 | channel(fB);
  }
};
} // namespace blink
