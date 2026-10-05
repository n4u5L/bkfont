// Ported from: blink/renderer/platform/fonts/simple_font_data.cc
// (Linux branch)
/*
 * Copyright (C) 2005, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Alexey Proskuryakov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "simple_font_data.h"

#include <unicode/utf16.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "base/allocator/partitions.h"
#include "base/math_extras.h"
#include "base/notreached.h"
#include "base/numerics/byte_conversions.h"
#include "base/text/character_names.h"
#include "base/text/unicode.h"
#include "font_cache.h"
#include "font_description.h"
#include "font_family_names.h"
#include "paint/scalar.h"
#include "platform/typeface.h"
#include "runtime_enabled_features.h"
#include "shaping/harfbuzz_face.h"
#include "shaping/harfbuzz_shaper.h"
#include "shaping/ng_shape_cache.h"
#include "shaping/opentype/open_type_baseline_metrics.h"
#include "shaping/opentype/open_type_vertical_data.h"
#include "paint/geometry.h"
#include "text_metrics.h"

namespace bkfont {

namespace {

// skia::DefaultFont()/DefaultTypeface(): the zero-size constructor still owns
// a default-size font. PlatformFont substitutes MakeEmpty() if matching fails.
PlatformFont DefaultFont() {
  return PlatformFont(FontCache::Get().GetFontManager()->LegacyMakeTypeface(
      String(), FontStyle()));
}

} // namespace

constexpr float kSmallCapsFontSizeMultiplier = 0.7f;
constexpr float kEmphasisMarkFontSizeMultiplier = 0.5f;

// sizeof(AF_LatinMetricsRec) = 2128
// TODO(drott): Measure a new number for Fontations.
constexpr int32_t kFontObjectsMemoryConsumption = 2128;

SimpleFontData::SimpleFontData(std::shared_ptr<const FontPlatformData> platform_data,
                               std::shared_ptr<const CustomFontData> custom_data, bool subpixel_ascent_descent,
                               const FontMetricsOverride& metrics_override)
    : platform_data_(std::move(platform_data)),
      shape_cache_(RuntimeEnabledFeatures::LayoutNGShapeCacheEnabled()
                       ? std::make_unique<NGShapeCache>(this)
                       : nullptr),
      font_(platform_data_->size() ? platform_data_->CreatePlatformFont()
                                   : DefaultFont()),
      custom_font_data_(std::move(custom_data)) {
  PlatformInit(subpixel_ascent_descent, metrics_override);
  PlatformGlyphInit();
}

SimpleFontData::~SimpleFontData() = default;

void SimpleFontData::PlatformInit(bool subpixel_ascent_descent,
                                  const FontMetricsOverride& metrics_override) {
  if (!platform_data_->size()) {
    font_metrics_.Reset();
    avg_char_width_ = 0;
    max_char_width_ = 0;
    return;
  }
  PlatformFontMetrics metrics;
  font_.GetMetrics(&metrics);

  float ascent;
  float descent;
  FontMetrics::AscentDescentWithHacks(
      ascent, descent, *platform_data_, font_, subpixel_ascent_descent,
      metrics_override.ascent_override, metrics_override.descent_override);
  font_metrics_.SetAscent(ascent);
  font_metrics_.SetDescent(descent);
  font_metrics_.SetCapHeight(metrics.cap_height);

  float underline_value;
  if (metrics.HasUnderlinePosition(&underline_value)) {
    font_metrics_.SetUnderlinePosition(underline_value);
  }
  if (metrics.HasUnderlineThickness(&underline_value)) {
    font_metrics_.SetUnderlineThickness(underline_value);
  }

  float x_height;
  if (metrics.x_height) {
    x_height = metrics.x_height;
    font_metrics_.SetXHeight(x_height);
  } else {
    x_height = ascent * 0.56; // Best guess from Windows font metrics.
    font_metrics_.SetXHeight(x_height);
    font_metrics_.SetHasXHeight(false);
  }
  const float line_gap = metrics_override.line_gap_override
                             ? *metrics_override.line_gap_override * platform_data_->size()
                             : metrics.leading;
  font_metrics_.SetLineGap(line_gap);
  font_metrics_.SetLineSpacing(lroundf(ascent) + lroundf(descent) + lroundf(line_gap));

  // Linux/FreeType's widget-width estimate, shared on every platform.
  // Better would be to rely on either max_char_width or avg_char_width.
  // skbug.com/3087
  max_char_width_ = FloatRoundToInt(metrics.x_max - metrics.x_min);

  if (metrics.avg_char_width) {
    avg_char_width_ = metrics.avg_char_width;
  } else {
    avg_char_width_ = x_height;
    const Glyph x_glyph = GlyphForCharacter('x');
    if (x_glyph) {
      avg_char_width_ = WidthForGlyph(x_glyph);
    }
  }

  OpenTypeBaselineMetrics m(PlatformData().GetHarfBuzzFace(), PlatformData().Orientation());
  font_metrics_.SetIdeographicBaseline(m.OpenTypeIdeographicBaseline());
  font_metrics_.SetAlphabeticBaseline(m.OpenTypeAlphabeticBaseline());
  font_metrics_.SetHangingBaseline(m.OpenTypeHangingBaseline());
}

void SimpleFontData::PlatformGlyphInit() {
  const FontPlatformData& platform_data = PlatformData();
  Typeface* typeface = platform_data.Typeface().get();

  if (!typeface->CountGlyphs()) {
    space_glyph_ = 0;
    space_width_ = 0;
    zero_glyph_ = 0;
    return;
  }

  // Nasty hack to determine if we should round or ceil space widths.
  // If the font is monospace or fake monospace we ceil to ensure that
  // every character and the space are the same width.  Otherwise we round.
  space_glyph_ = GlyphForCharacter(' ');
  float width = WidthForGlyph(space_glyph_);
  space_width_ = width;
  zero_glyph_ = GlyphForCharacter('0');

  font_metrics_.SetZeroWidth(ZeroInlineSize());
}

std::shared_ptr<const SimpleFontData> SimpleFontData::FontDataForCharacter(UChar32) const {
  return shared_from_this();
}

Glyph SimpleFontData::GlyphForCharacter(UChar32 codepoint) const {
  HarfBuzzFace* harfbuzz_face = PlatformData().GetHarfBuzzFace();
  if (!harfbuzz_face)
    return 0;
  // Retrieve glyph coverage information via HarfBuzz' character-to-glyph
  // mapping instead of the SkTypeface backend implementation so that it matches
  // the coverage we use through HarfBuzz during shaping. These two can differ
  // in situations where the system API synthesizes certain glyphs, see
  // https://crbug.com/1267606 for details. This function is used in situations
  // where CSS or layout (ellipsis, hyphenation) requires knowledge about a
  // particular character, hence it's important that they match.
  return harfbuzz_face->HbGlyphForCharacter(codepoint);
}

Glyph SimpleFontData::GlyphForMathCharacter(UChar32 codepoint,
                                            TextDirection direction) const {
  // If the text is RTL, try to get a suitable mirrored glyph. This is handled
  // automatically by harfbuzz when setting HB_DIRECTION_RTL in the buffer.
  if (RuntimeEnabledFeatures::MathMLOperatorRTLMirroringEnabled() && direction == TextDirection::kRtl) {
    StringBuilder builder;
    builder.Append(codepoint);
    HarfBuzzShaper shaper(builder.ToString());
    HarfBuzzShaper::GlyphDataList glyph_data_list;
    shaper.GetGlyphData(*this, LayoutLocale::GetDefault(), UScriptCode::USCRIPT_MATHEMATICAL_NOTATION,
                        /*is_horizontal=*/true,
                        direction,
                        glyph_data_list);
    // If found, return the first mirrored glyph.
    if (!glyph_data_list.empty()) {
      return glyph_data_list[0].glyph;
    }
  }

  // When a mirrored glyph can't be found, or when the text direction is LTR,
  // fall back to the original behaviour.
  return this->GlyphForCharacter(codepoint);
}

bool SimpleFontData::IsSegmented() const {
  return false;
}

std::shared_ptr<SimpleFontData> SimpleFontData::SmallCapsFontData(
    const FontDescription& font_description) const {
  if (!small_caps_) {
    small_caps_ =
        CreateScaledFontData(font_description, kSmallCapsFontSizeMultiplier);
  }
  return small_caps_;
}

std::shared_ptr<SimpleFontData> SimpleFontData::EmphasisMarkFontData(
    const FontDescription& font_description) const {
  if (!emphasis_mark_) {
    emphasis_mark_ =
        CreateScaledFontData(font_description, kEmphasisMarkFontSizeMultiplier);
  }
  return emphasis_mark_;
}

std::shared_ptr<SimpleFontData> SimpleFontData::CreateScaledFontData(
    const FontDescription& font_description,
    float scale_factor) const {
  const float scaled_size =
      lroundf(font_description.ComputedSize() * scale_factor);
  return std::make_shared<SimpleFontData>(
      std::make_shared<FontPlatformData>(*platform_data_, scaled_size),
      IsCustomFont() ? std::make_shared<CustomFontData>() : nullptr);
}

std::shared_ptr<SimpleFontData> SimpleFontData::MetricsOverriddenFontData(
    const FontMetricsOverride& metrics_override) const {
  return std::make_shared<SimpleFontData>(
      platform_data_,
      custom_font_data_,
      false /* subpixel_ascent_descent */,
      metrics_override);
}

// Internal leadings can be distributed to ascent and descent.
// -------------------------------------------
//           | - Internal Leading (in ascent)
//           |--------------------------------
//  Ascent - |              |
//           |              |
//           |              | - Em height
// ----------|--------------|
//           |              |
// Descent - |--------------------------------
//           | - Internal Leading (in descent)
// -------------------------------------------
FontHeight SimpleFontData::NormalizedTypoAscentAndDescent(
    FontBaseline baseline_type) const {
  if (baseline_type == kAlphabeticBaseline) {
    if (!normalized_typo_ascent_descent_.ascent)
      ComputeNormalizedTypoAscentAndDescent();
    return normalized_typo_ascent_descent_;
  }
  const LayoutUnit normalized_height =
      LayoutUnit::FromFloatRound(PlatformData().size());
  return {normalized_height - normalized_height / 2, normalized_height / 2};
}

LayoutUnit SimpleFontData::NormalizedTypoAscent(
    FontBaseline baseline_type) const {
  return NormalizedTypoAscentAndDescent(baseline_type).ascent;
}

LayoutUnit SimpleFontData::NormalizedTypoDescent(
    FontBaseline baseline_type) const {
  return NormalizedTypoAscentAndDescent(baseline_type).descent;
}

static std::pair<std::int16_t, std::int16_t> TypoAscenderAndDescender(Typeface* typeface) {
  std::int16_t buffer[2];
  std::size_t size = typeface->GetTableData(0x4f532f32, 68, sizeof(buffer), buffer); // 'OS/2'
  if (size == sizeof(buffer)) {
    // The buffer values are in big endian.
    return std::make_pair(base::ByteSwap(buffer[0]), -base::ByteSwap(buffer[1]));
  }
  return {0, 0};
}

void SimpleFontData::ComputeNormalizedTypoAscentAndDescent() const {
  // Compute em height metrics from OS/2 sTypoAscender and sTypoDescender.
  Typeface* typeface = platform_data_->Typeface().get();
  auto [typo_ascender, typo_descender] = TypoAscenderAndDescender(typeface);
  if (typo_ascender > 0 && TrySetNormalizedTypoAscentAndDescent(typo_ascender, typo_descender)) {
    return;
  }

  // As the last resort, compute em height metrics from our ascent/descent.
  const FontMetrics& font_metrics = GetFontMetrics();
  if (TrySetNormalizedTypoAscentAndDescent(font_metrics.FloatAscent(),
                                           font_metrics.FloatDescent())) {
    return;
  }

  // We shouldn't be here unless the height is zero or lower.
}

bool SimpleFontData::TrySetNormalizedTypoAscentAndDescent(float ascent,
                                                          float descent) const {
  const float height = ascent + descent;
  if (height <= 0 || ascent < 0 || ascent > height)
    return false;
  // While the OpenType specification recommends the sum of sTypoAscender and
  // sTypoDescender to equal 1em, most fonts do not follow. Most Latin fonts
  // set to smaller than 1em, and many tall scripts set to larger than 1em.
  // https://www.microsoft.com/typography/otspec/recom.htm#OS2
  // To ensure the sum of ascent and descent is the "em height", normalize by
  // keeping the ratio of sTypoAscender:sTypoDescender.
  // This matches to how Gecko computes "em height":
  // https://github.com/whatwg/html/issues/2470#issuecomment-291425136
  const float em_height = PlatformData().size();
  const LayoutUnit normalized_ascent =
      LayoutUnit::FromFloatRound(ascent * em_height / height);
  normalized_typo_ascent_descent_ = {
      normalized_ascent,
      LayoutUnit::FromFloatRound(em_height) - normalized_ascent};
  return true;
}

LayoutUnit SimpleFontData::VerticalPosition(
    FontVerticalPositionType position_type,
    FontBaseline baseline_type) const {
  switch (position_type) {
  case FontVerticalPositionType::TextTop:
    // Use Ascent, not FixedAscent, to match to how painter computes the
    // baseline position.
    return LayoutUnit(GetFontMetrics().Ascent(baseline_type));
  case FontVerticalPositionType::TextBottom:
    return LayoutUnit(-GetFontMetrics().Descent(baseline_type));
  case FontVerticalPositionType::TopOfEmHeight:
    return NormalizedTypoAscent(baseline_type);
  case FontVerticalPositionType::BottomOfEmHeight:
    return -NormalizedTypoDescent(baseline_type);
  }

  NOTREACHED();
}

const std::optional<float>& SimpleFontData::IdeographicAdvanceWidth() const {
  std::call_once(ideographic_advance_width_once_, [this] {
    // Use the advance of the CJK water character U+6C34 as the approximated
    // advance of fullwidth ideographic characters, as specified at
    // https://drafts.csswg.org/css-values-4/#ic.
    if (const Glyph cjk_water_glyph = GlyphForCharacter(uchar::kCjkWater)) {
      ideographic_advance_width_ = WidthForGlyph(cjk_water_glyph);
    }
  });
  return ideographic_advance_width_;
}

const std::optional<float>& SimpleFontData::IdeographicAdvanceHeight() const {
  std::call_once(ideographic_advance_height_once_, [this] {
    if (const Glyph cjk_water_glyph = GlyphForCharacter(uchar::kCjkWater)) {
      const HarfBuzzFace* hb_face = platform_data_->GetHarfBuzzFace();
      const OpenTypeVerticalData& vertical_data = hb_face->VerticalData();
      ideographic_advance_height_ =
          vertical_data.AdvanceHeight(cjk_water_glyph);
    }
  });
  return ideographic_advance_height_;
}

const std::optional<float>& SimpleFontData::IdeographicInlineSize() const {
  std::call_once(ideographic_inline_size_once_, [this] {
    // It should be computed without shaping; i.e., it doesn't include font
    // features, ligatures/kerning, nor `letter-spacing`.
    // https://github.com/w3c/csswg-drafts/issues/5498#issuecomment-686902802
    if (PlatformData().Orientation() != FontOrientation::kVerticalUpright) {
      ideographic_inline_size_ = IdeographicAdvanceWidth();
      return;
    }

    // Compute vertical advance if the orientation is `kVerticalUpright`.
    ideographic_inline_size_ = IdeographicAdvanceHeight();
  });
  return ideographic_inline_size_;
}

float SimpleFontData::TextAutoSpaceInlineSize() const {
  return IdeographicInlineSize().value_or(PlatformData().size()) / 8;
}

const HanKerning::FontData& SimpleFontData::HanKerningData(
    const LayoutLocale& locale,
    bool is_horizontal) const {
  for (const HanKerningCacheEntry& entry : han_kerning_cache_) {
    if (entry.locale.get() == &locale && entry.is_horizontal == is_horizontal) {
      return entry.data;
    }
  }

  // The cache didn't hit. Shift the list and create a new entry at `[0]`.
  for (wtf_size_t i = 1; i < std::size(han_kerning_cache_); ++i) {
    han_kerning_cache_[i] = std::move(han_kerning_cache_[i - 1]);
  }
  HanKerningCacheEntry& new_entry = han_kerning_cache_[0];
  new_entry = {.locale = LayoutLocale::Get(locale.LocaleString()),
               .is_horizontal = is_horizontal,
               .data = HanKerning::FontData(*this, locale, is_horizontal)};
  return new_entry.data;
}

RectF SimpleFontData::PlatformBoundsForGlyph(Glyph glyph) const {
  if (!platform_data_->size()) {
    return RectF();
  }

  static_assert(sizeof(glyph) == 2, "Glyph id should not be truncated.");
  ScalarRect bounds;
  FontGetBoundsForGlyph(font_, glyph, &bounds);
  return RectF(bounds.left, bounds.top, bounds.Width(), bounds.Height());
}

void SimpleFontData::BoundsForGlyphs(std::span<const Glyph> glyphs,
                                     std::span<RectF> bounds) const {
  if (!platform_data_->size()) {
    return;
  }

  // The port's callers use RectF. Keep the upstream batched metrics call
  // and convert its LTRB rectangles to XYWH only at this boundary.
  Vector<ScalarRect, 256> glyph_bounds(glyphs.size());
  FontGetBoundsForGlyphs(font_, glyphs, glyph_bounds.data());
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    const ScalarRect& rect = glyph_bounds[i];
    bounds[i] = RectF(rect.left, rect.top, rect.Width(), rect.Height());
  }
}

float SimpleFontData::WidthForGlyph(Glyph glyph) const {
  if (!platform_data_->size()) {
    return 0;
  }

  static_assert(sizeof(glyph) == 2, "Glyph id should not be truncated.");
  return FontGetWidthForGlyph(font_, glyph);
}

float SimpleFontData::ZeroInlineSize() const {
  // The advance measure of a glyph depends on writing-mode and text-orientation
  // as well as font settings, text-transform, and any other properties that
  // affect glyph selection or orientation.
  // ref: https://drafts.csswg.org/css-values-4/#ch
  if (zero_glyph_) {
    if (PlatformData().Orientation() != FontOrientation::kVerticalUpright) {
      // Use the advance of the “0” (ZERO, U+0030) as the approximated
      // advance of European alphanumeric characters as specified at
      // https://drafts.csswg.org/css-values-4/#ch.
      return WidthForGlyph(zero_glyph_);
    } else {
      const HarfBuzzFace* hb_face = platform_data_->GetHarfBuzzFace();
      const OpenTypeVerticalData& vertical_data = hb_face->VerticalData();
      return vertical_data.AdvanceHeight(zero_glyph_);
    }
  }

  // In the cases where it is impossible or impractical to determine the
  // measure of the “0” glyph, it must be assumed to be 0.5em wide by 1em
  // tall. Thus, the ch unit falls back to 0.5em in the general case, and to
  // 1em when it would be typeset upright (i.e. writing-mode is vertical-rl or
  // vertical-lr and text-orientation is upright).
  const float size = font_.GetSize();
  if (!platform_data_->IsVerticalNonCJKUpright()) {
    if (RuntimeEnabledFeatures::CSSChUnitSpecCompliantFallbackEnabled()) {
      return size * 0.5f;
    }

    // This is a bug that was unfortunately introduced in
    // crrev.com/c/6333369, and has shipped in m136.
    // TODO(crbug.com/416145497): Remove this old behaviour
    // when feature flag
    // `RuntimeEnabledFeatures::CSSChUnitSpecCompliantFallbackEnabled` is tested
    // out and is enabled by default.
    return size / 0.5f;
  }
  return size;
}

} // namespace bkfont
