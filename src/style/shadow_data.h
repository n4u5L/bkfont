// Ported from: blink/renderer/core/style/shadow_data.h
// Ported from: blink/renderer/core/style/shadow_data.cc
/*
 * Copyright (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <cassert>
#include <cmath>

#include "paint/geometry.h"
#include "style/style_color.h"

namespace bkfont {

// skia_utils.h BlurRadiusToStdDev().
inline float BlurRadiusToStdDev(float radius) {
  assert(radius >= 0);
  // Per spec, sigma is exactly half the blur radius:
  // https://www.w3.org/TR/css-backgrounds-3/#shadow-blur
  // https://html.spec.whatwg.org/C/#when-shadows-are-drawn
  return radius * 0.5f;
}

enum class ShadowStyle { kNormal, kInset };

// This class holds information about shadows for the text-shadow property.
class ShadowData {
public:
  ShadowData(Vector2dF offset, float blur, float spread, ShadowStyle style, StyleColorValue color,
             float opacity = 1.0f)
      : offset_(offset), blur_(blur, blur), spread_(spread), color_(color), style_(style), opacity_(opacity) {}

  ShadowData(Vector2dF offset, PointF blur, float spread, ShadowStyle style, StyleColorValue color,
             float opacity = 1.0f)
      : offset_(offset), blur_(blur), spread_(spread), color_(color), style_(style), opacity_(opacity) {}

  bool operator==(const ShadowData&) const = default;

  static ShadowData NeutralValue() {
    return ShadowData(Vector2dF(0, 0), 0, 0, ShadowStyle::kNormal, StyleColorValue(Color::kTransparent));
  }

  float X() const { return offset_.x(); }
  float Y() const { return offset_.y(); }
  Vector2dF Offset() const { return offset_; }
  float Blur() const { return blur_.x(); }
  PointF BlurXY() const { return blur_; }
  float Spread() const { return spread_; }
  ShadowStyle Style() const { return style_; }
  const StyleColorValue& GetColor() const { return color_; }
  float Opacity() const { return opacity_; }

  // Outsets needed to adjust a source rectangle to the one cast by this
  // shadow.
  OutsetsF RectOutsets() const {
    // 3 * sigma is how Skia computes the box blur extent.
    // See also https://crbug.com/624175.
    float blur_and_spread = std::ceil(3 * BlurRadiusToStdDev(Blur())) + Spread();
    return OutsetsF()
        .set_left(blur_and_spread - X())
        .set_right(blur_and_spread + X())
        .set_top(blur_and_spread - Y())
        .set_bottom(blur_and_spread + Y());
  }

private:
  Vector2dF offset_;
  PointF blur_;
  float spread_;
  StyleColorValue color_;
  ShadowStyle style_;
  float opacity_;
};

} // namespace bkfont
