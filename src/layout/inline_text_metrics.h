// Ported from: blink/renderer/core/style/computed_style.cc
// Ported from: blink/renderer/core/layout/inline/inline_box_state.cc
// Ported from: blink/renderer/core/layout/inline/line_utils.cc
// Ported from: blink/renderer/core/layout/logical_box_fragment.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "font/font_baseline.h"
#include "font/font_height.h"
#include "geometry/length.h"
#include "layout/layout_unit.h"

namespace bkit {

class Font;
class FontDescription;

// ComputedStyle::GetFontBaseline() for 'dominant-baseline: auto', the only
// value for non-SVG elements. Vertical flow (except
// 'text-orientation: sideways') uses the ideographic central baseline, whose
// position FontMetrics derives from the font height when the font has no
// baseline table.
FontBaseline GetFontBaseline(const FontDescription&);

// ComputedStyle::ComputedFontSizeAsFixed().
LayoutUnit ComputedFontSizeAsFixed(const Font&);

// ComputedStyle::ComputedLineHeightAsFixed(). `line_height` is the computed
// 'line-height': Length::Auto() for 'normal', a percentage for a number or a
// percentage, or a fixed length.
LayoutUnit ComputedLineHeightAsFixed(const Length& line_height, const Font&);

// CalculateLeadingSpace() from line_utils.cc: splits the half-leading between
// the ascent and descent sides.
FontHeight CalculateLeadingSpace(const LayoutUnit& line_height, const FontHeight& current_height);

// The InlineBoxState fields ComputeTextMetrics() sets.
struct InlineTextMetrics {
  // The primary font metrics for the baseline with the half-leading of
  // 'line-height' added. The line box metrics unite these.
  FontHeight text_metrics;
  // The block offset and height of text fragments relative to the baseline,
  // from the primary font metrics before the half-leading is added.
  LayoutUnit text_top;
  LayoutUnit text_height;
};

// InlineBoxState::ComputeTextMetrics() for a non-SVG inline box without
// text-fit scaling or emphasis marks.
InlineTextMetrics ComputeTextMetrics(const Font&, const Length& line_height, FontBaseline baseline_type);

// LogicalBoxFragment::BaselineMetrics() when the box has no baseline in the
// parent's writing mode (e.g. an orthogonal inline-block), with the initial
// 'margin-box' baseline edge and no margins. `block_size` is the size of the
// box in the block direction of the parent line.
FontHeight SynthesizeBaselineMetrics(LayoutUnit block_size, FontBaseline baseline_type);

} // namespace bkit
