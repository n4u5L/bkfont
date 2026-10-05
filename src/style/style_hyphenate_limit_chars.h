// Ported from: blink/renderer/core/style/style_hyphenate_limit_chars.h
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cstdint>

#include "base/numerics/safe_conversions.h"

namespace bkfont {

// Style data for the `hyphenate-limit-chars` property.
// https://w3c.github.io/csswg-drafts/css-text-4/#propdef-hyphenate-limit-chars
class StyleHyphenateLimitChars {
public:
  StyleHyphenateLimitChars() = default;
  StyleHyphenateLimitChars(unsigned min_word_chars, unsigned min_before_chars, unsigned min_after_chars)
      : min_word_chars_(base::saturated_cast<uint8_t>(min_word_chars)),
        min_before_chars_(base::saturated_cast<uint8_t>(min_before_chars)),
        min_after_chars_(base::saturated_cast<uint8_t>(min_after_chars)) {}

  // The minimum number of characters in the word / before the hyphen / after
  // the hyphen. `0` means `auto`, the UA chooses a value that adapts to the
  // current layout.
  unsigned MinWordChars() const { return min_word_chars_; }
  unsigned MinBeforeChars() const { return min_before_chars_; }
  unsigned MinAfterChars() const { return min_after_chars_; }

  bool IsAuto() const { return !min_word_chars_ && !min_before_chars_ && !min_after_chars_; }

  bool operator==(const StyleHyphenateLimitChars& other) const {
    return min_word_chars_ == other.min_word_chars_ && min_before_chars_ == other.min_before_chars_ &&
           min_after_chars_ == other.min_after_chars_;
  }
  bool operator!=(const StyleHyphenateLimitChars& other) const { return !operator==(other); }

private:
  uint8_t min_word_chars_ = 0;
  uint8_t min_before_chars_ = 0;
  uint8_t min_after_chars_ = 0;
};

} // namespace bkfont
