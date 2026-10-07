// Ported from: blink/renderer/platform/fonts/simple_font_data.h
// (Linux branch)
/*
 * This file is part of the internal font implementation.
 *
 * Copyright (C) 2006, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2007-2008 Torch Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <memory>
#include <mutex>
#include <span>
#include <utility>

#include "canvas_rotation_in_vertical.h"
#include "custom_font_data.h"
#include "font_baseline.h"
#include "font_data.h"
#include "font_metrics.h"
#include "font_metrics_override.h"
#include "font_platform_data.h"
#include "font_vertical_position_type.h"
#include "glyph.h"
#include "shaping/han_kerning.h"
#include "typesetting_features.h"
#include "base/casting.h"
#include "base/text/string_hash.h"
#include "platform/platform_font.h"
#include "paint/geometry.h"

namespace bkit {

class NGShapeCache;

// Holds the glyph index and the corresponding SimpleFontData information for a
// given
// character.
struct GlyphData {

public:
  GlyphData(
      Glyph g = 0,
      std::shared_ptr<const SimpleFontData> f = nullptr,
      CanvasRotationInVertical rotation = CanvasRotationInVertical::kRegular)
      : glyph(g),
        font_data(std::move(f)),
        canvas_rotation(rotation) {
  }
  Glyph glyph;
  std::shared_ptr<const SimpleFontData> font_data;
  CanvasRotationInVertical canvas_rotation;
};

class FontDescription;

class SimpleFontData final : public FontData, public std::enable_shared_from_this<SimpleFontData> {
public:
  // Used to create platform fonts.
  SimpleFontData(
      std::shared_ptr<const FontPlatformData>,
      std::shared_ptr<const CustomFontData> custom_data = nullptr,
      bool subpixel_ascent_descent = false,
      const FontMetricsOverride& metrics_override = FontMetricsOverride());

  SimpleFontData(const SimpleFontData&) = delete;
  SimpleFontData(SimpleFontData&&) = delete;
  ~SimpleFontData() override;
  SimpleFontData& operator=(const SimpleFontData&) = delete;
  SimpleFontData& operator=(const SimpleFontData&&) = delete;

  const FontPlatformData& PlatformData() const {
    return *platform_data_;
  }
  NGShapeCache& GetShapeCache() const {
    return *shape_cache_;
  }

  std::shared_ptr<SimpleFontData> SmallCapsFontData(const FontDescription&) const;
  std::shared_ptr<SimpleFontData> EmphasisMarkFontData(const FontDescription&) const;
  std::shared_ptr<SimpleFontData> MetricsOverriddenFontData(const FontMetricsOverride&) const;

  FontMetrics& GetFontMetrics() {
    return font_metrics_;
  }
  const FontMetrics& GetFontMetrics() const {
    return font_metrics_;
  }
  float InternalLeading() const {
    return GetFontMetrics().FloatHeight() - PlatformData().size();
  }

  // The approximated advance of fullwidth ideographic characters in the inline
  // axis. This is currently used to support the `ic` unit.
  // https://drafts.csswg.org/css-values-4/#ic
  const std::optional<float>& IdeographicInlineSize() const;
  const std::optional<float>& IdeographicAdvanceWidth() const;
  const std::optional<float>& IdeographicAdvanceHeight() const;

  // The inter-script spacing by the CSS `text-autospace` property.
  // https://drafts.csswg.org/css-text-4/#inter-script-spacing
  float TextAutoSpaceInlineSize() const;

  // The approximated advance of “0” (ZERO, U+0030) character in the inline
  // axis. This is currently used to support the `ch` unit.
  // https://drafts.csswg.org/css-values-4/#ch
  float ZeroInlineSize() const;

  // |sTypoAscender| and |sTypoDescender| in |OS/2| table, normalized to 1em.
  // This metrics can simulate ideographics em-box when the font doesn't have
  // better ways to compute it.
  // https://docs.microsoft.com/en-us/typography/opentype/spec/baselinetags#ideoembox
  FontHeight NormalizedTypoAscentAndDescent(
      FontBaseline baseline_type = kAlphabeticBaseline) const;
  LayoutUnit NormalizedTypoAscent(FontBaseline = kAlphabeticBaseline) const;
  LayoutUnit NormalizedTypoDescent(FontBaseline = kAlphabeticBaseline) const;

  LayoutUnit VerticalPosition(FontVerticalPositionType, FontBaseline) const;

  float MaxCharWidth() const {
    return max_char_width_;
  }
  void SetMaxCharWidth(float max_char_width) {
    max_char_width_ = max_char_width;
  }

  float AvgCharWidth() const {
    return avg_char_width_;
  }
  void SetAvgCharWidth(float avg_char_width) {
    avg_char_width_ = avg_char_width;
  }

  const HanKerning::FontData& HanKerningData(const LayoutLocale& locale,
                                             bool is_horizontal) const;

  RectF BoundsForGlyph(Glyph) const;
  void BoundsForGlyphs(std::span<const Glyph>, std::span<RectF>) const;
  RectF PlatformBoundsForGlyph(Glyph) const;
  float WidthForGlyph(Glyph) const;

  float SpaceWidth() const {
    return space_width_;
  }
  void SetSpaceWidth(float space_width) {
    space_width_ = space_width;
  }

  Glyph SpaceGlyph() const {
    return space_glyph_;
  }
  void SetSpaceGlyph(Glyph space_glyph) {
    space_glyph_ = space_glyph;
  }
  Glyph ZeroGlyph() const {
    return zero_glyph_;
  }
  void SetZeroGlyph(Glyph zero_glyph) {
    zero_glyph_ = zero_glyph;
  }

  std::shared_ptr<const SimpleFontData> FontDataForCharacter(UChar32) const override;

  Glyph GlyphForCharacter(UChar32) const;
  Glyph GlyphForMathCharacter(UChar32, TextDirection) const;

  bool IsCustomFont() const override {
    return static_cast<bool>(custom_font_data_);
  }
  bool IsLoading() const override {
    return custom_font_data_ ? custom_font_data_->IsLoading() : false;
  }
  bool IsLoadingFallback() const override {
    return custom_font_data_ ? custom_font_data_->IsLoadingFallback() : false;
  }
  bool IsPendingDataUrlCustomFont() const {
    return custom_font_data_ ? custom_font_data_->IsPendingDataUrl() : false;
  }
  bool IsSegmented() const override;
  bool ShouldSkipDrawing() const override {
    return custom_font_data_ && custom_font_data_->ShouldSkipDrawing();
  }

  const CustomFontData* GetCustomFontData() const {
    return custom_font_data_.get();
  }

private:
  void PlatformInit(bool subpixel_ascent_descent, const FontMetricsOverride&);
  void PlatformGlyphInit();

  std::shared_ptr<SimpleFontData> CreateScaledFontData(const FontDescription&,
                                                       float scale_factor) const;

  void ComputeNormalizedTypoAscentAndDescent() const;
  bool TrySetNormalizedTypoAscentAndDescent(float ascent, float descent) const;

  FontMetrics font_metrics_;
  float max_char_width_ = -1;
  float avg_char_width_ = -1;

  std::shared_ptr<const FontPlatformData> platform_data_;
  std::unique_ptr<NGShapeCache> shape_cache_;
  const PlatformFont font_;

  Glyph space_glyph_ = 0;
  float space_width_ = 0;
  Glyph zero_glyph_ = 0;

  mutable std::shared_ptr<SimpleFontData> small_caps_;
  mutable std::shared_ptr<SimpleFontData> emphasis_mark_;

  std::shared_ptr<const CustomFontData> custom_font_data_;

  mutable std::once_flag ideographic_inline_size_once_;
  mutable std::once_flag ideographic_advance_width_once_;
  mutable std::once_flag ideographic_advance_height_once_;
  mutable std::optional<float> ideographic_inline_size_;
  mutable std::optional<float> ideographic_advance_width_;
  mutable std::optional<float> ideographic_advance_height_;

  // Simple LRU cache for `HanKerning::FontData`. The cache has 2 entries
  // because one additional language or horizontal/vertical mixed document is
  // possible, but more than that are very unlikely.
  struct HanKerningCacheEntry {
    scoped_refptr<const LayoutLocale> locale;
    bool is_horizontal;
    HanKerning::FontData data;
  };
  mutable HanKerningCacheEntry han_kerning_cache_[2];

  mutable FontHeight normalized_typo_ascent_descent_;
};

inline RectF SimpleFontData::BoundsForGlyph(Glyph glyph) const {
  return PlatformBoundsForGlyph(glyph);
}

template <>
struct DowncastTraits<SimpleFontData> {
  static bool AllowFrom(const FontData& fontData) {
    return !fontData.IsSegmented();
  }
};

} // namespace bkit
