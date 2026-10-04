// Copyright 2012 Google Inc. All rights reserved.
// BSD license retained in the companion harfbuzz_face.cc from Chromium.
// Port source: platform/fonts/shaping/harfbuzz_face.h.
#pragma once
#include <cstdint>
#include <memory>
#include <hb.h>
#include <hb-cplusplus.hh>
#include "font/glyph.h"
#include "font/typesetting_features.h"
#include "font/unicode_range_set.h"
#include "variation_selector_mode.h"

namespace bkfont {
class FontPlatformData;
class OpenTypeVerticalData;
struct HarfBuzzFontData;
class HarfBuzzFace final {
public:
  explicit HarfBuzzFace(const FontPlatformData*, uint64_t unique_id = 0);
  ~HarfBuzzFace();
  HarfBuzzFace(const HarfBuzzFace&) = delete;
  HarfBuzzFace& operator=(const HarfBuzzFace&) = delete;
  enum VerticalLayoutCallbacks {
    kPrepareForVerticalLayout,
    kNoVerticalLayout
  };
  hb_font_t* GetScaledFont(const UnicodeRangeSet*, VerticalLayoutCallbacks,
                           float specified_size) const;
  hb_font_t* GetScaledFont() const;
  bool HasSpaceInLigaturesOrKerning(TypesettingFeatures);
  unsigned UnitsPerEmFromHeadTable();
  Glyph HbGlyphForCharacter(UChar32 character);
  hb_codepoint_t HarfBuzzGetGlyphForTesting(UChar32, UChar32);
  bool ShouldSubpixelPosition();
  const OpenTypeVerticalData& VerticalData() const;
  static void Init();
  static VariationSelectorMode GetVariationSelectorMode();
  static void SetVariationSelectorMode(VariationSelectorMode);

private:
  const FontPlatformData* platform_data_;
  std::shared_ptr<HarfBuzzFontData> harfbuzz_font_data_;
};
inline constexpr hb_codepoint_t kUnmatchedVSGlyphId = static_cast<hb_codepoint_t>(-1);
} // namespace bkfont
