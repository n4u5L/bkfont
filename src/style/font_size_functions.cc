// Ported from: blink/renderer/core/css/font_size_functions.cc
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

#include "font_size_functions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "font/font_description.h"
#include "font/simple_font_data.h"
#include "style/computed_style_constants.h"
#include "style/style_host_context.h"

namespace bkit {
namespace {

const int kFontSizeTableMax = 16;
const int kDefaultMediumFontSize = 12;
const int kFontSizeTableMin = 9;
const int kTotalKeywords = 8;

constexpr auto kFontSizeTableSize = kFontSizeTableMax - kFontSizeTableMin + 1;

using FontSizeTableType = std::array<std::array<int, kTotalKeywords>, kFontSizeTableSize>;

// Strict mode table matches MacIE and Mozilla's settings exactly. There is no
// quirks mode here.
const FontSizeTableType kStrictFontSizeTable{{
    {9, 9, 9, 9, 11, 14, 18, 27},
    {9, 9, 9, 10, 12, 15, 20, 30},
    {9, 9, 10, 11, 13, 17, 22, 33},
    {9, 9, 10, 12, 14, 18, 24, 36},
    {9, 10, 12, 13, 16, 20, 26, 39}, // fixed font default (13)
    {9, 10, 12, 14, 17, 21, 28, 42},
    {9, 10, 13, 15, 18, 23, 30, 45},
    {9, 10, 13, 16, 18, 24, 32, 48} // proportional font default (16)
}};
// HTML       1      2      3      4      5      6      7
// CSS  xxs   xs     s      m      l     xl     xxl
//                          |
//                      user pref

// For values outside the range of the table, we use Todd Fahrner's suggested
// scale factors for each keyword value.
const std::array<float, kTotalKeywords> kFontSizeFactors{0.60f, 0.75f, 0.89f, 1.0f, 1.2f, 1.5f, 2.0f, 3.0f};

inline int RowFromMediumFontSizeInRange(const Settings* settings, bool is_monospace, int& medium_size) {
  medium_size =
      settings ? (is_monospace ? settings->default_fixed_font_size : settings->default_font_size) : kDefaultMediumFontSize;
  if (medium_size >= kFontSizeTableMin && medium_size <= kFontSizeTableMax) return medium_size - kFontSizeTableMin;
  return -1;
}

template <typename T>
int FindNearestLegacyFontSize(int pixel_font_size, const std::array<T, kTotalKeywords>& table, int multiplier) {
  // Ignore table[0] because xx-small does not correspond to any legacy font
  // size.
  for (int i = 1; i < kTotalKeywords - 1; i++) {
    if (pixel_font_size * 2 < (table[i] + table[i + 1]) * multiplier) return i;
  }
  return kTotalKeywords - 1;
}

float AspectValue(const SimpleFontData& font_data, FontSizeAdjust::Metric metric, float computed_size) {
  const FontMetrics& font_metrics = font_data.GetFontMetrics();
  // We return fallback values for missing font metrics.
  // https://github.com/w3c/csswg-drafts/issues/6384
  float aspect_value = 1.0;
  switch (metric) {
    case FontSizeAdjust::Metric::kCapHeight:
      if (font_metrics.CapHeight() > 0) aspect_value = font_metrics.CapHeight() / computed_size;
      break;
    case FontSizeAdjust::Metric::kChWidth:
      if (font_metrics.HasZeroWidth()) aspect_value = font_metrics.ZeroWidth() / computed_size;
      break;
    case FontSizeAdjust::Metric::kIcWidth:
      if (const std::optional<float>& size = font_data.IdeographicAdvanceWidth()) aspect_value = *size / computed_size;
      break;
    case FontSizeAdjust::Metric::kIcHeight:
      if (const std::optional<float>& size = font_data.IdeographicAdvanceHeight())
        aspect_value = *size / computed_size;
      break;
    case FontSizeAdjust::Metric::kExHeight:
    default:
      if (font_metrics.HasXHeight()) aspect_value = font_metrics.XHeight() / computed_size;
  }
  return aspect_value;
}

} // namespace

float FontSizeFunctions::GetComputedSizeFromSpecifiedSize(const Settings* settings, float zoom_factor,
                                                          bool is_absolute_size, float specified_size,
                                                          ApplyMinimumFontSize apply_minimum_font_size) {
  // Text with a 0px font size should not be visible and therefore needs to be
  // exempt from minimum font size rules. Acid3 relies on this for pixel-perfect
  // rendering. This is also compatible with other browsers that have minimum
  // font size settings (e.g. Firefox).
  if (std::fabs(specified_size) < std::numeric_limits<float>::epsilon()) return 0.0f;

  if (apply_minimum_font_size && settings) {
    // We support two types of minimum font size. The first is a hard override
    // that applies to all fonts. This is "min_size." The second type of minimum
    // font size is a "smart minimum" that is applied only when the Web page
    // can't know what size it really asked for, e.g., when it uses logical
    // sizes like "small" or expresses the font-size as a percentage of the
    // user's default font setting.

    // With the smart minimum, we never want to get smaller than the minimum
    // font size to keep fonts readable. However we always allow the page to set
    // an explicit pixel size that is smaller, since sites will mis-render
    // otherwise (e.g., http://www.gamespot.com with a 9px minimum).

    int min_size = settings->minimum_font_size;
    int min_logical_size = settings->minimum_logical_font_size;

    // Apply the hard minimum first.
    if (specified_size < min_size) specified_size = min_size;

    // Now apply the "smart minimum". The font size must either be relative to
    // the user default or the original size must have been acceptable. In other
    // words, we only apply the smart minimum whenever we're positive doing so
    // won't disrupt the layout.
    if (specified_size < min_logical_size && !is_absolute_size) specified_size = min_logical_size;
  }
  // Also clamp to a reasonable maximum to prevent insane font sizes from
  // causing crashes on various platforms (I'm looking at you, Windows.)
  return std::min(kMaximumAllowedFontSize, specified_size * zoom_factor);
}

float FontSizeFunctions::FontSizeForKeyword(const Settings* settings, unsigned keyword, bool is_monospace) {
  assert(keyword >= 1u && keyword <= 8u);
  int medium_size = 0;
  int row = RowFromMediumFontSizeInRange(settings, is_monospace, medium_size);
  if (row >= 0) {
    int col = (keyword - 1);
    return kStrictFontSizeTable[row][col];
  }

  // Value is outside the range of the table. Apply the scale factor instead.
  float min_logical_size = settings ? std::max(settings->minimum_logical_font_size, 1) : 1;
  return std::max(kFontSizeFactors[keyword - 1] * medium_size, min_logical_size);
}

int FontSizeFunctions::LegacyFontSize(const Settings* settings, int pixel_font_size, bool is_monospace) {
  if (!settings) return 1;
  int medium_size = 0;
  int row = RowFromMediumFontSizeInRange(settings, is_monospace, medium_size);
  if (row >= 0) return FindNearestLegacyFontSize<int>(pixel_font_size, kStrictFontSizeTable[row], 1);
  return FindNearestLegacyFontSize<float>(pixel_font_size, kFontSizeFactors, medium_size);
}

std::optional<float> FontSizeFunctions::FontAspectValue(const SimpleFontData* font_data, FontSizeAdjust::Metric metric,
                                                        float computed_size) {
  if (!font_data || !computed_size) return std::nullopt;
  float aspect_value = AspectValue(*font_data, metric, computed_size);
  if (!aspect_value) return std::nullopt;
  return aspect_value;
}

std::optional<float> FontSizeFunctions::MetricsMultiplierAdjustedFontSize(const SimpleFontData* font_data,
                                                                          const FontDescription& font_description) {
  assert(font_data);
  const float computed_size = font_description.ComputedSize();
  const FontSizeAdjust size_adjust = font_description.SizeAdjust();
  if (!computed_size || size_adjust.Value() == FontSizeAdjust::kFontSizeAdjustNone) return std::nullopt;
  float aspect_value = AspectValue(*font_data, size_adjust.GetMetric(), computed_size);
  if (!aspect_value) return std::nullopt;
  return (size_adjust.Value() / aspect_value) * computed_size;
}

} // namespace bkit
