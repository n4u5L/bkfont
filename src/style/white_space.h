// Ported from: blink/renderer/core/css/white_space.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Subset: `text-wrap-mode` is not ported yet (InlineLayoutOptions::wrap), so
// the `white-space` shorthand values and ShouldWrapLine() are omitted.
#pragma once

#include <cstdint>

namespace bkfont {

enum class WhiteSpaceCollapse : uint8_t {
  kCollapse = 0,
  kPreserve = 1,
  // `KPreserve` is a bit-flag, but bit 2 is shared by two different behaviors
  // below to save memory. Use functions below instead of direct comparisons.
  kPreserveBreaks = 2,
  kBreakSpaces = kPreserve | 2,
  // Ensure `kWhiteSpaceCollapseBits` can hold all values.
};

static constexpr int kWhiteSpaceCollapseBits = 2;
static constexpr uint8_t kWhiteSpaceCollapseMask = (1 << kWhiteSpaceCollapseBits) - 1;

inline bool IsWhiteSpaceCollapseAny(WhiteSpaceCollapse value, WhiteSpaceCollapse flags) {
  return static_cast<uint8_t>(value) & static_cast<uint8_t>(flags);
}

inline bool ShouldPreserveWhiteSpaces(WhiteSpaceCollapse collapse) {
  return IsWhiteSpaceCollapseAny(collapse, WhiteSpaceCollapse::kPreserve);
}
inline bool ShouldCollapseWhiteSpaces(WhiteSpaceCollapse collapse) {
  return !ShouldPreserveWhiteSpaces(collapse);
}
inline bool ShouldPreserveBreaks(WhiteSpaceCollapse collapse) {
  return collapse != WhiteSpaceCollapse::kCollapse;
}
inline bool ShouldCollapseBreaks(WhiteSpaceCollapse collapse) {
  return !ShouldPreserveBreaks(collapse);
}
inline bool ShouldBreakSpaces(WhiteSpaceCollapse collapse) {
  return collapse == WhiteSpaceCollapse::kBreakSpaces;
}

} // namespace bkfont
