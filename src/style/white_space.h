// Ported from: blink/renderer/core/css/white_space.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <bit>
#include <cstdint>

#include "style/computed_style_base_constants.h"

namespace bkit {

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

inline constexpr unsigned kTextWrapModeBits =
    std::bit_width(static_cast<unsigned>(TextWrapMode::kMaxEnumValue));

inline bool ShouldWrapLine(TextWrapMode mode) {
  return mode != TextWrapMode::kNowrap;
}

// EWhiteSpace packs both longhands, including combinations without a named
// shorthand keyword. IsValidWhiteSpace() recognizes the predefined keywords.
constexpr uint8_t ToWhiteSpaceValue(WhiteSpaceCollapse collapse, TextWrapMode wrap) {
  return static_cast<uint8_t>(collapse) | (static_cast<uint8_t>(wrap) << kWhiteSpaceCollapseBits);
}

enum class EWhiteSpace : uint8_t {
  kNormal = ToWhiteSpaceValue(WhiteSpaceCollapse::kCollapse, TextWrapMode::kWrap),
  kNowrap = ToWhiteSpaceValue(WhiteSpaceCollapse::kCollapse, TextWrapMode::kNowrap),
  kPre = ToWhiteSpaceValue(WhiteSpaceCollapse::kPreserve, TextWrapMode::kNowrap),
  kPreLine = ToWhiteSpaceValue(WhiteSpaceCollapse::kPreserveBreaks, TextWrapMode::kWrap),
  kPreWrap = ToWhiteSpaceValue(WhiteSpaceCollapse::kPreserve, TextWrapMode::kWrap),
  kBreakSpaces = ToWhiteSpaceValue(WhiteSpaceCollapse::kBreakSpaces, TextWrapMode::kWrap)
};

static_assert(kWhiteSpaceCollapseBits + kTextWrapModeBits <= sizeof(EWhiteSpace) * 8);

inline EWhiteSpace ToWhiteSpace(WhiteSpaceCollapse collapse, TextWrapMode wrap) {
  return static_cast<EWhiteSpace>(ToWhiteSpaceValue(collapse, wrap));
}

inline bool IsValidWhiteSpace(EWhiteSpace whitespace) {
  return whitespace == EWhiteSpace::kNormal || whitespace == EWhiteSpace::kNowrap ||
         whitespace == EWhiteSpace::kPre || whitespace == EWhiteSpace::kPreLine ||
         whitespace == EWhiteSpace::kPreWrap || whitespace == EWhiteSpace::kBreakSpaces;
}

inline WhiteSpaceCollapse ToWhiteSpaceCollapse(EWhiteSpace whitespace) {
  return static_cast<WhiteSpaceCollapse>(static_cast<uint8_t>(whitespace) & kWhiteSpaceCollapseMask);
}

inline TextWrapMode ToTextWrapMode(EWhiteSpace whitespace) {
  return static_cast<TextWrapMode>(static_cast<uint8_t>(whitespace) >> kWhiteSpaceCollapseBits);
}

} // namespace bkit
