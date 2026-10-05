// Ported from: blink/renderer/core/style/applied_text_decoration.h
// Ported from: blink/renderer/core/style/applied_text_decoration.cc
// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// The color is the resolved paint color (Color4f), as the local
// ComputedStyle stores 'color' through its legacy paint.
#pragma once

#include "base/vector.h"
#include "geometry/length.h"
#include "paint/color4f.h"
#include "style/computed_style_base_constants.h"
#include "style/text_decoration_thickness.h"

namespace bkfont {

class AppliedTextDecoration {
public:
  AppliedTextDecoration(TextDecorationLine line, ETextDecorationStyle style, Color4f color,
                        TextDecorationThickness thickness, Length underline_offset)
      : lines_(static_cast<unsigned>(line)), style_(static_cast<unsigned>(style)), color_(color),
        thickness_(thickness), underline_offset_(underline_offset) {}

  TextDecorationLine Lines() const { return static_cast<TextDecorationLine>(lines_); }
  ETextDecorationStyle Style() const { return static_cast<ETextDecorationStyle>(style_); }
  Color4f GetColor() const { return color_; }
  void SetColor(Color4f color) { color_ = color; }

  TextDecorationThickness Thickness() const { return thickness_; }
  Length UnderlineOffset() const { return underline_offset_; }

  bool operator==(const AppliedTextDecoration& o) const {
    return color_ == o.color_ && lines_ == o.lines_ && style_ == o.style_ && thickness_ == o.thickness_ &&
           underline_offset_ == o.underline_offset_;
  }
  bool operator!=(const AppliedTextDecoration& o) const { return !(*this == o); }

private:
  unsigned lines_ : kTextDecorationLineBits;
  unsigned style_ : 3; // ETextDecorationStyle
  Color4f color_;
  TextDecorationThickness thickness_;
  Length underline_offset_;
};

using AppliedTextDecorationVector = Vector<AppliedTextDecoration, 1>;

} // namespace bkfont
