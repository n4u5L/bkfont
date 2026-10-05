// Ported from: blink/renderer/platform/fonts/shaping/harfbuzz_font_data.h

// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cmath>
#include <memory>

#include <hb-cplusplus.hh>

#include "font/font_metrics.h"
#include "font/font_platform_data.h"
#include "harfbuzz_face.h"
#include "platform/platform_font.h"
#include "shaping/opentype/open_type_vertical_data.h"

namespace bkfont {

inline constexpr unsigned kInvalidFallbackMetricsValue = static_cast<unsigned>(-1);

// The HarfBuzzFontData struct carries font data and the vertical data for a
// HarfBuzz font.
struct HarfBuzzFontData final {
public:
  explicit HarfBuzzFontData(hb_font_t* unscaled_font)
      : unscaled_font_(hb::unique_ptr<hb_font_t>(unscaled_font)),
        vertical_data_(nullptr),
        range_set_(nullptr) {
  }

  HarfBuzzFontData(const HarfBuzzFontData&) = delete;
  HarfBuzzFontData& operator=(const HarfBuzzFontData&) = delete;

  // The vertical origin and vertical advance functions in HarfBuzzFace require
  // the ascent and height metrics as fallback in case no specific vertical
  // layout information is found from the font.
  void UpdateFallbackMetricsAndScale(
      const FontPlatformData& platform_data,
      HarfBuzzFace::VerticalLayoutCallbacks vertical_layout) {
    float ascent = 0;
    float descent = 0;

    font_ = platform_data.CreatePlatformFont();

    if (vertical_layout == HarfBuzzFace::kPrepareForVerticalLayout)
        [[unlikely]] {
      FontMetrics::AscentDescentWithHacks(ascent, descent, platform_data,
                                          font_);
      ascent_fallback_ = ascent;
      // Simulate the rounding that FontMetrics does so far for returning the
      // integer Height()
      height_fallback_ = static_cast<float>(lroundf(ascent) + lroundf(descent));

      int units_per_em =
          static_cast<int>(platform_data.GetHarfBuzzFace()->UnitsPerEmFromHeadTable());
      size_per_unit_ = platform_data.size() / (units_per_em ? units_per_em : 1);
    } else {
      ascent_fallback_ = kInvalidFallbackMetricsValue;
      height_fallback_ = kInvalidFallbackMetricsValue;
      size_per_unit_ = kInvalidFallbackMetricsValue;
    }
  }

  OpenTypeVerticalData* VerticalData() {
    if (!vertical_data_) {
      vertical_data_ =
          std::make_unique<OpenTypeVerticalData>(*font_.GetTypeface());
    }
    vertical_data_->SetScaleAndFallbackMetrics(size_per_unit_, ascent_fallback_,
                                               static_cast<int>(height_fallback_));
    return vertical_data_.get();
  }

  const hb::unique_ptr<hb_font_t> unscaled_font_;
  PlatformFont font_;

  // Capture these scaled fallback metrics from FontPlatformData so that a
  // OpenTypeVerticalData object can be constructed from them when needed.
  float size_per_unit_ = 0;
  float ascent_fallback_ = 0;
  float height_fallback_ = 0;

  enum class SpaceGlyphInOpenTypeTables {
    kUnknown,
    kPresent,
    kNotPresent
  };

  SpaceGlyphInOpenTypeTables space_in_gpos_ =
      SpaceGlyphInOpenTypeTables::kUnknown;
  SpaceGlyphInOpenTypeTables space_in_gsub_ =
      SpaceGlyphInOpenTypeTables::kUnknown;

  std::unique_ptr<OpenTypeVerticalData> vertical_data_;
  // UnicodeRangeSet is immutable. Keep its value after shaping, as the source
  // strong Member does, without retaining a GC-managed object.
  std::unique_ptr<const UnicodeRangeSet> range_set_;
};

} // namespace bkfont
