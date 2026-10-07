// Ported from: blink/renderer/core/css/css_color.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "base/text/wtf_string.h"
#include "paint/color.h"
#include "style/css_value.h"

namespace bkit {
namespace cssvalue {

// Represents the non-keyword subset of <color>.
class CSSColor : public CSSValue {
public:
  static scoped_refptr<const CSSColor> Create(const Color& color) {
    return base::AdoptRef(new CSSColor(color));
  }

  CSSColor(Color color)
      : CSSValue(kColorClass),
        color_(color) {
  }

  String CustomCSSText() const {
    return SerializeAsCSSComponentValue(color_);
  }

  Color Value() const {
    return color_;
  }

  bool Equals(const CSSColor& other) const {
    return color_ == other.color_;
  }

  // Returns the color serialized according to CSSOM:
  // https://drafts.csswg.org/cssom/#serialize-a-css-component-value
  static String SerializeAsCSSComponentValue(Color color) {
    return color.SerializeAsCSSColor();
  }

private:
  Color color_;
};

} // namespace cssvalue

template <>
struct DowncastTraits<cssvalue::CSSColor> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsColorValue();
  }
};

} // namespace bkit
