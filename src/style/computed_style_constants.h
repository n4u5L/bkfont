// Subset of: blink/renderer/core/style/computed_style_constants.h
/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2006 Graham Dennis (graham.dennis@gmail.com)
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
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

#include <cstddef>

namespace bkfont {

// Random visual rendering model attributes. Not inherited.

enum class EVerticalAlign : unsigned {
  kBaseline,
  kMiddle,
  kSub,
  kSuper,
  kTextTop,
  kTextBottom,
  kTop,
  kBottom,
  kBaselineMiddle,
  kLength
};

static const size_t kTextUnderlinePositionBits = 4;
enum class TextUnderlinePosition : unsigned {
  kAuto = 0x0,
  kFromFont = 0x1,
  kUnder = 0x2,
  kLeft = 0x4,
  kRight = 0x8
};
inline TextUnderlinePosition operator|(TextUnderlinePosition a, TextUnderlinePosition b) {
  return TextUnderlinePosition(int(a) | int(b));
}
inline TextUnderlinePosition& operator|=(TextUnderlinePosition& a, TextUnderlinePosition b) {
  return a = a | b;
}

// Reasonable maximum to prevent insane font sizes from causing crashes on some
// platforms (such as Windows).
static const float kMaximumAllowedFontSize = 10000.0f;

enum class TextEmphasisPosition : unsigned {
  kOverRight,
  kOverLeft,
  kUnderRight,
  kUnderLeft,
  kAuto,
};

inline bool IsOver(TextEmphasisPosition position) {
  return position == TextEmphasisPosition::kOverRight || position == TextEmphasisPosition::kOverLeft;
}

inline bool IsRight(TextEmphasisPosition position) {
  return position == TextEmphasisPosition::kOverRight || position == TextEmphasisPosition::kUnderRight;
}

inline bool IsLeft(TextEmphasisPosition position) {
  return !IsRight(position);
}

enum class LineLogicalSide {
  kOver,
  kUnder,
};

} // namespace bkfont
