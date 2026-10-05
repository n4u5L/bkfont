// Ported from: blink/renderer/core/style/computed_style.cc
// Ported from: blink/renderer/core/layout/inline/inline_box_state.cc
// Ported from: blink/renderer/core/layout/inline/line_utils.cc
// Ported from: blink/renderer/core/layout/logical_box_fragment.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inline_text_metrics.h"

#include "font/font.h"
#include "font/font_description.h"
#include "font/simple_font_data.h"
#include "geometry/length_functions.h"

namespace bkfont {

FontBaseline GetFontBaseline(const FontDescription& font_description) {
  // Vertical flow (except 'text-orientation: sideways') uses ideographic
  // central baseline.
  // https://drafts.csswg.org/css-writing-modes-3/#text-baselines
  return !font_description.IsVerticalAnyUpright() ? kAlphabeticBaseline : kCentralBaseline;
}

LayoutUnit ComputedFontSizeAsFixed(const Font& font) {
  return LayoutUnit::FromFloatRound(font.GetFontDescription().ComputedSize());
}

LayoutUnit ComputedLineHeightAsFixed(const Length& lh, const Font& font) {
  // For "normal" line-height use the font's built-in spacing if available.
  if (lh.IsAuto()) {
    if (font.PrimaryFont()) {
      return font.PrimaryFont()->GetFontMetrics().FixedLineSpacing();
    }
    return LayoutUnit();
  }

  if (lh.HasPercent()) {
    return MinimumValueForLength(lh, ComputedFontSizeAsFixed(font));
  }

  return LayoutUnit::FromFloatRound(lh.Pixels());
}

FontHeight CalculateLeadingSpace(const LayoutUnit& line_height, const FontHeight& current_height) {
  // TODO(kojii): floor() is to make text dump compatible with legacy test
  // results. Revisit when we paint.
  LayoutUnit ascent_leading_spacing{((line_height - current_height.LineHeight()) / 2).Floor()};
  LayoutUnit descent_leading_spacing = line_height - current_height.LineHeight() - ascent_leading_spacing;
  return FontHeight(ascent_leading_spacing, descent_leading_spacing);
}

InlineTextMetrics ComputeTextMetrics(const Font& font, const Length& line_height, FontBaseline baseline_type) {
  InlineTextMetrics result;
  if (const SimpleFontData* font_data = font.PrimaryFont()) {
    result.text_metrics = font_data->GetFontMetrics().GetFontHeight(baseline_type);
  } else {
    result.text_metrics = FontHeight();
  }
  result.text_top = -result.text_metrics.ascent;
  result.text_height = result.text_metrics.LineHeight();

  FontHeight leading_space = CalculateLeadingSpace(ComputedLineHeightAsFixed(line_height, font), result.text_metrics);
  result.text_metrics.AddLeading(leading_space);
  return result;
}

FontHeight SynthesizeBaselineMetrics(LayoutUnit size, FontBaseline baseline_type) {
  return baseline_type == kAlphabeticBaseline ? FontHeight(size, LayoutUnit())
                                              : FontHeight(size - size / 2, size / 2);
}

} // namespace bkfont
