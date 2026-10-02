// Ported from Chromium: third_party/blink/renderer/platform/fonts/shaping/font_features.h
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include "base/containers/span.h"
#include <optional>

#include "wtf/vector.h"

struct hb_feature_t;

namespace blink {

class FontDescription;

//
// Represents an OpenType font feature tag.
//
// This struct is compatible with `hb_tag_t`.
//
struct FontFeatureTag {
  constexpr FontFeatureTag() = default;
  constexpr FontFeatureTag(uint32_t tag)
      : tag(tag) {
  }
  constexpr FontFeatureTag(uint8_t c1, uint8_t c2, uint8_t c3, uint8_t c4)
      : tag((((((static_cast<uint32_t>(c1) << 8) | c2) << 8) | c3) << 8) | c4) {
  }

  operator uint32_t() const {
    return tag;
  }

  uint32_t tag = 0;
};

//
// Represents an OpenType font feature with its value.
//
struct FontFeatureValue : public FontFeatureTag {
  uint32_t value = 0;
};

//
// Represents an OpenType font feature with its value and text range.
//
struct FontFeatureRange : public FontFeatureValue {
  // The size produced by `FromFontDescription()` for the initial style.
  static constexpr wtf_size_t kInitialSize = 1;

  // Initialize the list from |FontDescription|.
  template <wtf_size_t InlineCapacity>
  static void FromFontDescription(const FontDescription&,
                                  Vector<FontFeatureRange, InlineCapacity>&);

  // True if the list is for the initial style.
  static bool IsInitial(base::span<const FontFeatureRange>);

  // This struct has the same size and layout as `hb_feature_t`.
  static const hb_feature_t* ToHarfBuzzData(const FontFeatureRange* features) {
    return reinterpret_cast<const hb_feature_t*>(features);
  }

  uint32_t start = 0;
  uint32_t end = static_cast<uint32_t>(-1);
};

//
// Represents a list of `FontFeatureRange`.
//
using FontFeatureRanges = Vector<FontFeatureRange, 6>;

//
// Saves `FontFeatureRanges` and restores it in the destructor.
//
// It works only for additions.
//
class FontFeatureRangesSaver {

public:
  explicit FontFeatureRangesSaver(FontFeatureRanges* features)
      : features_(features),
        num_features_before_(features->size()) {
  }

  ~FontFeatureRangesSaver() {
    if (features_->size() > num_features_before_) {
      features_->Shrink(num_features_before_);
    }
  }

private:
  FontFeatureRanges* features_;
  wtf_size_t num_features_before_;
};

} // namespace blink
