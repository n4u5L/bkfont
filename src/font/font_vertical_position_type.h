// Port source: third_party/blink/renderer/platform/fonts/font_vertical_position_type.h
// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <cmath>
#include "base/vector.h"

namespace bkfont {

enum class FontVerticalPositionType {
  // TextTop and TextBottom are the top/bottom of the content area.
  // This is where 'vertical-align: text-top/text-bottom' aligns to.
  // This is explicitly undefined in CSS2.
  // https://drafts.csswg.org/css2/visudet.html#inline-non-replaced
  TextTop,
  TextBottom,
  // Em height as being discussed in Font Metrics API.
  // https://drafts.css-houdini.org/font-metrics-api-1/#fontmetrics
  TopOfEmHeight,
  BottomOfEmHeight
};

// Returns whether the position type is CSS "line-over"; i.e., ascender side
// or "top" side of a line box.
// https://drafts.csswg.org/css-writing-modes-3/#line-over
inline bool IsLineOverSide(FontVerticalPositionType type) {
  return type == FontVerticalPositionType::TextTop || type == FontVerticalPositionType::TopOfEmHeight;
}

} // namespace bkfont
