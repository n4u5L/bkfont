// Ported from: blink/renderer/platform/text/tab_size.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "runtime_enabled_features.h"
#include "base/allocator/allocator.h"

namespace bkfont {

enum class TabSizeValueType {
  kLength,
  kSpace
};

struct TabSize {
  TabSize(float num_or_length,
          TabSizeValueType is_spaces = TabSizeValueType::kSpace)
      : float_value_(num_or_length),
        is_spaces_(static_cast<unsigned>(is_spaces)) {
  }

  bool IsSpaces() const {
    return is_spaces_;
  }

  float GetPixelSize(float space_width,
                     float letter_spacing = 0.0f,
                     float word_spacing = 0.0f) const {
    if (!RuntimeEnabledFeatures::TabSizeWithSpacingEnabled()) {
      return is_spaces_ ? float_value_ * space_width : float_value_;
    }
    return is_spaces_
               ? float_value_ * (space_width + letter_spacing + word_spacing)
               : float_value_;
  }

  float float_value_;
  unsigned is_spaces_ : 1;
};

inline bool operator==(const TabSize& a, const TabSize& b) {
  return (a.float_value_ == b.float_value_) && (a.is_spaces_ == b.is_spaces_);
}

inline bool operator!=(const TabSize& a, const TabSize& b) {
  return !(a == b);
}

} // namespace bkfont
