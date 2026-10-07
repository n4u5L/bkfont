// Ported from: blink/renderer/core/css/font_size_functions.h
/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
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
//
// The Document argument is replaced by the host Settings. The document is
// always in standards mode.
#pragma once

#include <cassert>
#include <optional>

#include "font/font_size_adjust.h"
#include "style/css_value_keywords.h"

namespace bkit {

class FontDescription;
class SimpleFontData;
struct Settings;

enum ApplyMinimumFontSize {
  kDoNotApplyMinimumForFontSize,
  kApplyMinimumForFontSize
};

class FontSizeFunctions {
public:
  FontSizeFunctions() = delete;

  static float GetComputedSizeFromSpecifiedSize(const Settings*, float zoom_factor, bool is_absolute_size,
                                                float specified_size,
                                                ApplyMinimumFontSize = kApplyMinimumForFontSize);

  // Given a CSS keyword in the range (xx-small to xxx-large), this function
  // returns values from '1' to '8'.
  static unsigned KeywordSize(CSSValueID value_id) {
    assert(IsValidValueID(value_id));
    if (value_id == CSSValueID::kWebkitXxxLarge) value_id = CSSValueID::kXxxLarge;
    return static_cast<int>(value_id) - static_cast<int>(CSSValueID::kXxSmall) + 1;
  }

  static bool IsValidValueID(CSSValueID value_id) {
    return (value_id >= CSSValueID::kXxSmall && value_id <= CSSValueID::kXxxLarge) ||
           value_id == CSSValueID::kWebkitXxxLarge;
  }

  static CSSValueID InitialValueID() { return CSSValueID::kMedium; }
  static unsigned InitialKeywordSize() { return KeywordSize(InitialValueID()); }

  // Given a keyword size in the range (1 to 8), this function will return
  // the correct font size scaled relative to the user's default (4).
  static float FontSizeForKeyword(const Settings*, unsigned keyword, bool is_monospace);

  // Given a font size in pixel, this function will return legacy font size
  // between 1 and 7.
  static int LegacyFontSize(const Settings*, int pixel_font_size, bool is_monospace);

  // Given font data, this function returns a normalized aspect value for the
  // specified font metric, which is calculated using the size of the font
  // metric divided by the font size.
  // https://www.w3.org/TR/css-fonts-5/#font-size-adjust-aspect-value
  static std::optional<float> FontAspectValue(const SimpleFontData*, FontSizeAdjust::Metric, float computed_size);

  // Given font data, this function returns a font size adjusted by
  // font-size-adjust, scaling the font size to achieve the desired aspect
  // value of font size to metric.
  static std::optional<float> MetricsMultiplierAdjustedFontSize(const SimpleFontData*, const FontDescription&);
};

} // namespace bkit
