// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license.
// Ported from platform/fonts/shaping/harfbuzz_font_data.h.
#pragma once
#include <cmath>
#include <memory>
#include <hb-cplusplus.hh>
#include "font/font_platform_data.h"
#include "harfbuzz_face.h"
#include "shaping/opentype/open_type_vertical_data.h"
namespace bkfont {
inline constexpr unsigned kInvalidFallbackMetricsValue = static_cast<unsigned>(-1);
struct HarfBuzzFontData final {
  explicit HarfBuzzFontData(hb_font_t* font)
      : unscaled_font_(font) {
  }
  HarfBuzzFontData(const HarfBuzzFontData&) = delete;
  HarfBuzzFontData& operator=(const HarfBuzzFontData&) = delete;
  void UpdateFallbackMetricsAndScale(const FontPlatformData&,
                                     HarfBuzzFace::VerticalLayoutCallbacks);
  OpenTypeVerticalData* VerticalData() {
    if (!vertical_data_)
      vertical_data_ = std::make_unique<OpenTypeVerticalData>(platform_data_->Face());
    vertical_data_->SetScaleAndFallbackMetrics(size_per_unit_, ascent_fallback_, height_fallback_);
    return vertical_data_.get();
  }
  const hb::unique_ptr<hb_font_t> unscaled_font_;
  PlatformFont font_;
  // Owning value snapshot replaces the source SkFont value; no pointer back
  // to any FontPlatformData/HarfBuzzFace that retains this shared cache entry.
  std::unique_ptr<FontPlatformData> platform_data_;
  float size_per_unit_ = 0;
  float ascent_fallback_ = 0;
  float height_fallback_ = 0;
  enum class SpaceGlyphInOpenTypeTables {
    kUnknown,
    kPresent,
    kNotPresent
  };
  SpaceGlyphInOpenTypeTables space_in_gpos_ = SpaceGlyphInOpenTypeTables::kUnknown;
  SpaceGlyphInOpenTypeTables space_in_gsub_ = SpaceGlyphInOpenTypeTables::kUnknown;
  std::unique_ptr<OpenTypeVerticalData> vertical_data_;
  // UnicodeRangeSet is immutable. Keep its value after shaping, as the source
  // strong Member does, without retaining a GC-managed object.
  std::unique_ptr<const UnicodeRangeSet> range_set_;
};
} // namespace bkfont
