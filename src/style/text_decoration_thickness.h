// Ported from: blink/renderer/core/style/text_decoration_thickness.h
// Ported from: blink/renderer/core/style/text_decoration_thickness.cc
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cassert>

#include "geometry/length.h"
#include "style/css_value_keywords.h"

namespace bkfont {

class TextDecorationThickness {
public:
  TextDecorationThickness() : thickness_(Length::Auto()) {}

  explicit TextDecorationThickness(const Length& length) : thickness_(length) {}

  explicit TextDecorationThickness(CSSValueID from_font_keyword) {
    assert(from_font_keyword == CSSValueID::kFromFont);
    (void)from_font_keyword;
    thickness_from_font_ = true;
  }

  bool IsFromFont() const { return thickness_from_font_; }
  const Length& Thickness() const {
    assert(!thickness_from_font_);
    return thickness_;
  }
  bool IsAuto() const { return !thickness_from_font_ && thickness_.IsAuto(); }

  bool operator==(const TextDecorationThickness& other) const {
    return thickness_from_font_ == other.thickness_from_font_ && thickness_ == other.thickness_;
  }
  bool operator!=(const TextDecorationThickness& other) const { return !(*this == other); }

private:
  Length thickness_;
  bool thickness_from_font_{false};
};

} // namespace bkfont
