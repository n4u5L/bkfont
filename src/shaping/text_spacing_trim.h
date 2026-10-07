// Ported from: blink/renderer/platform/fonts/shaping/text_spacing_trim.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace bkit {

// Values for the `text-spacing-trim` property.
// https://drafts.csswg.org/css-text-4/#text-spacing-trim-property
enum class TextSpacingTrim {
  kNormal,
  kSpaceAll,
  kSpaceFirst,
  kTrimStart,

  kInitial = kNormal,
};

inline constexpr unsigned kTextSpacingTrimBitCount = 2;

inline bool ShouldTrimAdjacent(TextSpacingTrim value) {
  return value != TextSpacingTrim::kSpaceAll;
}

inline bool ShouldTrimStartOfParagraph(TextSpacingTrim value) {
  return value == TextSpacingTrim::kTrimStart;
}

inline bool ShouldTrimStartOfWrappedLine(TextSpacingTrim value) {
  return value == TextSpacingTrim::kSpaceFirst || value == TextSpacingTrim::kTrimStart;
}

inline bool ShouldTrimEnd(TextSpacingTrim value) {
  return value != TextSpacingTrim::kSpaceAll;
}

} // namespace bkit
