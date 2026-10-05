// Ported from: blink/renderer/platform/text/writing_mode.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace bkfont {

// These values are named to match the CSS keywords they correspond to: namely
// horizontal-tb, vertical-rl and vertical-lr.
// Since these names aren't very self-explanatory, where possible use the
// inline utility functions below.
enum class WritingMode : uint8_t {
  kHorizontalTb = 0,
  kVerticalRl = 1,
  kVerticalLr = 2,
  // sideways-rl and sideways-lr are only supported by LayoutNG.
  kSidewaysRl = 3,
  kSidewaysLr = 4,

  kMaxWritingMode = kSidewaysLr,
};

// Lines have horizontal orientation; modes horizontal-tb.
inline bool IsHorizontalWritingMode(WritingMode writing_mode) {
  return writing_mode == WritingMode::kHorizontalTb;
}

// Lines have vertical orientation; modes vertical-lr, vertical-rl.
inline bool IsVerticalWritingMode(WritingMode writing_mode) {
  return writing_mode == WritingMode::kVerticalLr || writing_mode == WritingMode::kVerticalRl;
}

// Bottom of the line occurs earlier in the block; modes vertical-lr.
inline bool IsFlippedLinesWritingMode(WritingMode writing_mode) {
  return writing_mode == WritingMode::kVerticalLr;
}

// In flipped-lines writing mode, 'line-over' and 'block-start' don't match.
// When dealing with the logical coordinate system in the [line-relative
// directions], 'vertical-lr' has 'line-over' on right, which is equivalent to
// the 'vertical-rl' in the flow-relative directions.
// https://drafts.csswg.org/css-writing-modes-3/#line-directions
inline WritingMode ToLineWritingMode(WritingMode writing_mode) {
  return !IsFlippedLinesWritingMode(writing_mode) ? writing_mode : WritingMode::kVerticalRl;
}

// Block progression increases in the opposite direction to normal; modes
// vertical-rl and sideways-rl.
inline bool IsFlippedBlocksWritingMode(WritingMode writing_mode) {
  return writing_mode == WritingMode::kVerticalRl || writing_mode == WritingMode::kSidewaysRl;
}

// Whether the child and the containing block are parallel to each other.
// Example: vertical-rl and vertical-lr
inline bool IsParallelWritingMode(WritingMode a, WritingMode b) {
  return (a == WritingMode::kHorizontalTb) == (b == WritingMode::kHorizontalTb);
}

// Returns true if the specified writing-mode is a horizontal typographic
// mode; modes horizontal-tb, sideways-lr, and sideways-rl.
// https://drafts.csswg.org/css-writing-modes/#horizontal-typographic-mode
inline bool IsHorizontalTypographicMode(WritingMode writing_mode) {
  return writing_mode == WritingMode::kHorizontalTb || writing_mode == WritingMode::kSidewaysLr ||
         writing_mode == WritingMode::kSidewaysRl;
}

} // namespace bkfont
