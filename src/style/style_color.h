// Adapted from: blink/renderer/core/css/style_color.h
// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Computed color that may be currentcolor, resolved at use time. Local
// subset: no system colors, color-mix() or unresolved color functions.
#pragma once

#include "paint/color.h"
#include "paint/color4f.h"
#include "style/css_color.h"
#include "style/css_identifier_value.h"

namespace bkit {

// Color::toSkColor4f() (local ToColorFloat4()). The standalone ComputedStyle
// stores resolved paint colors, so it converts where paint would.
inline Color4f ToColor4f(const Color& color) {
  const ColorFloat4 value = color.ToColorFloat4();
  return {value.fR, value.fG, value.fB, value.fA};
}

class StyleColorValue {
public:
  static StyleColorValue CurrentColor() {
    return StyleColorValue();
  }
  explicit StyleColorValue(Color color)
      : color_(color),
        current_color_(false) {
  }
  bool IsCurrentColor() const {
    return current_color_;
  }
  Color GetColor() const {
    return color_;
  }
  Color4f Resolve(Color4f current) const {
    return current_color_ ? current : ToColor4f(color_);
  }
  scoped_refptr<const CSSValue> ToCSSValue() const {
    if (current_color_) return CSSIdentifierValue::Create(CSSValueID::kCurrentcolor);
    return cssvalue::CSSColor::Create(color_);
  }
  bool operator==(const StyleColorValue&) const = default;

private:
  StyleColorValue() = default;
  Color color_;
  bool current_color_ = true;
};

} // namespace bkit
