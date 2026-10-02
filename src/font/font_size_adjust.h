// Port source: third_party/blink/renderer/platform/fonts/font_size_adjust.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "wtf/forward.h"

#include <cstdint>
#include "wtf/text/wtf_string.h"

namespace blink {

class FontSizeAdjust {
public:
  enum class Metric {
    kExHeight,
    kCapHeight,
    kChWidth,
    kIcWidth,
    kIcHeight
  };
  enum class ValueType : bool {
    kNumber,
    kFromFont
  };

  FontSizeAdjust() = default;
  explicit FontSizeAdjust(float value)
      : value_(value) {
  }
  explicit FontSizeAdjust(float value, ValueType type)
      : value_(value),
        type_(type) {
  }
  explicit FontSizeAdjust(float value, Metric metric)
      : value_(value),
        metric_(metric) {
  }
  explicit FontSizeAdjust(float value, Metric metric, ValueType type)
      : value_(value),
        metric_(metric),
        type_(type) {
  }

  static constexpr float kFontSizeAdjustNone = -1;

  explicit operator bool() const {
    return value_ != kFontSizeAdjustNone || type_ == ValueType::kFromFont;
  }
  bool operator==(const FontSizeAdjust& other) const {
    return value_ == other.Value() && metric_ == other.GetMetric() && IsFromFont() == other.IsFromFont();
  }
  bool operator!=(const FontSizeAdjust& other) const {
    return !operator==(other);
  }

  bool IsFromFont() const {
    return type_ == ValueType::kFromFont;
  }
  float Value() const {
    return value_;
  }
  Metric GetMetric() const {
    return metric_;
  }

  unsigned GetHash() const;
  String ToString() const;

private:
  String ToString(Metric metric) const;

  float value_{kFontSizeAdjustNone};
  Metric metric_{Metric::kExHeight};
  ValueType type_{ValueType::kNumber};
};

} // namespace blink
