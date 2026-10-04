/*
 * Copyright (C) 2011 Brent Fulgham
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

// Port source: platform/fonts/font_platform_data.cc (Windows and backend-independent paths).
#include "font_platform_data.h"
#include "shaping/harfbuzz_face.h"
#include <cstring>
#include <cmath>
#include "base/math_extras.h"
#include "font_cache.h"
#include "runtime_enabled_features.h"
namespace bkfont {
FontPlatformData::FontPlatformData() = default;
FontPlatformData::FontPlatformData(HashTableDeletedValueType)
    : is_hash_table_deleted_value_(true) {
}
FontPlatformData::FontPlatformData(const FontPlatformData& source)
    : typeface_(source.typeface_),
      text_size_(source.text_size_),
      synthetic_bold_(source.synthetic_bold_),
      synthetic_italic_(source.synthetic_italic_),
      avoid_embedded_bitmaps_(source.avoid_embedded_bitmaps_),
      text_rendering_(source.text_rendering_),
      orientation_(source.orientation_),
      resolved_font_features_(source.resolved_font_features_),
      use_anti_alias_(source.use_anti_alias_),
      use_subpixel_rendering_(source.use_subpixel_rendering_) {
}
FontPlatformData::FontPlatformData(const FontPlatformData& src, float text_size)
    : FontPlatformData(src.typeface_, String(), text_size, src.synthetic_bold_,
                       src.synthetic_italic_, src.text_rendering_, src.resolved_font_features_, src.orientation_) {
}
FontPlatformData::FontPlatformData(std::shared_ptr<FontFace> face, const String&,
                                   float size, bool bold, bool italic, TextRenderingMode rendering,
                                   ResolvedFontFeatures features, FontOrientation orientation)
    : typeface_(std::move(face)),
      text_size_(size),
      synthetic_bold_(bold),
      synthetic_italic_(italic),
      text_rendering_(rendering),
      orientation_(orientation),
      resolved_font_features_(std::move(features)),
      use_anti_alias_(!RuntimeEnabledFeatures::NoFontAntialiasingEnabled() && FontCache::AntialiasedTextEnabled()),
      use_subpixel_rendering_(use_anti_alias_ && FontCache::LcdTextEnabled()) {
}
FontPlatformData::~FontPlatformData() = default;
bool FontPlatformData::operator==(const FontPlatformData& a) const {
  const bool equal_faces = (!typeface_ || !a.typeface_)
                               ? typeface_ == a.typeface_
                               : typeface_->UniqueId() == a.typeface_->UniqueId();
  return equal_faces && text_size_ == a.text_size_ && is_hash_table_deleted_value_ == a.is_hash_table_deleted_value_ && synthetic_bold_ == a.synthetic_bold_ && synthetic_italic_ == a.synthetic_italic_ && avoid_embedded_bitmaps_ == a.avoid_embedded_bitmaps_ && text_rendering_ == a.text_rendering_ && resolved_font_features_ == a.resolved_font_features_ && use_anti_alias_ == a.use_anti_alias_ && use_subpixel_rendering_ == a.use_subpixel_rendering_ && orientation_ == a.orientation_;
}
uint64_t FontPlatformData::UniqueID() const {
  return typeface_->UniqueId();
}
String FontPlatformData::FontFamilyName() const {
  return typeface_->FamilyName();
}
String FontPlatformData::GetPostScriptName() const {
  return typeface_->PostScriptName();
}
bool FontPlatformData::IsAhem() const {
  return FontFamilyName() == "Ahem";
}
HarfBuzzFace* FontPlatformData::GetHarfBuzzFace() const {
  if (!harfbuzz_face_) harfbuzz_face_ = std::make_unique<HarfBuzzFace>(this, UniqueID());
  return harfbuzz_face_.get();
}
bool FontPlatformData::HasSpaceInLigaturesOrKerning(TypesettingFeatures features) const {
  HarfBuzzFace* face = GetHarfBuzzFace();
  return face && face->HasSpaceInLigaturesOrKerning(features);
}
unsigned FontPlatformData::GetHash() const {
  unsigned h = static_cast<unsigned>(UniqueID());
  h ^= 0x01010101 * ((static_cast<int>(is_hash_table_deleted_value_) << 3) | (static_cast<int>(orientation_) << 2) | (static_cast<int>(synthetic_bold_) << 1) | static_cast<int>(synthetic_italic_));
  uint32_t size_bytes;
  std::memcpy(&size_bytes, &text_size_, sizeof(uint32_t));
  return h ^ size_bytes;
}
bool FontPlatformData::FontContainsCharacter(UChar32 character) const {
  return typeface_->ContainsCharacter(character);
}
} // namespace bkfont

namespace bkfont {
FontRenderOptions FontPlatformData::RenderOptions() const {
  FontRenderOptions options;
  options.synthetic_bold = synthetic_bold_;
  options.synthetic_italic = synthetic_italic_;
  options.anti_alias = use_anti_alias_;
  options.subpixel_rendering = use_subpixel_rendering_;
  options.subpixel_positioning = use_anti_alias_;
  options.embedded_bitmaps = !avoid_embedded_bitmaps_;
  return options;
}
PlatformFont FontPlatformData::CreatePlatformFont(const FontDescription*) const {
  PlatformFont font(typeface_);
  font.SetSize(text_size_);
  font.SetEmbolden(synthetic_bold_);
  font.SetSkewX(synthetic_italic_ ? -1.0f / 4 : 0);

  bool use_subpixel_rendering = use_subpixel_rendering_;
  bool use_anti_alias = use_anti_alias_;

  if (use_subpixel_rendering) {
    font.SetEdging(PlatformFont::Edging::kSubpixelAntiAlias);
  } else if (use_anti_alias) {
    font.SetEdging(PlatformFont::Edging::kAntiAlias);
  } else {
    font.SetEdging(PlatformFont::Edging::kAlias);
  }

  // Only use sub-pixel positioning if anti aliasing is enabled. Otherwise,
  // without font smoothing, subpixel text positioning leads to uneven spacing
  // since subpixel test placement coordinates would be passed to Skia, which
  // only has non-antialiased glyphs to draw, so they necessarily get clamped at
  // pixel positions, which leads to uneven spacing, either too close or too far
  // away from adjacent glyphs. We avoid this by linking the two flags.
  if (use_anti_alias) {
    font.SetSubpixel(true);
  }

  // The WebTestSupport subpixel override has no counterpart here.

  font.SetEmbeddedBitmaps(!avoid_embedded_bitmaps_);
  return font;
}
bool FontPlatformData::MeasureGlyph(uint16_t glyph, PlatformGlyphMetrics* metrics) const {
  const bool measured = typeface_->MeasureGlyph(glyph, text_size_, RenderOptions(), metrics);
  // Source: skia/skia_text_metrics.cc. Keep Blink's final rounding outside
  // the native scaler, whose values correspond to the underlying font API.
  if (!ShouldSubpixelPosition()) {
    metrics->advance_x = ClampTo<int32_t>(std::floor(metrics->advance_x + 0.5f));
    if (measured) {
      const float right = ClampTo<int32_t>(std::ceil(metrics->left + metrics->width));
      const float bottom = ClampTo<int32_t>(std::ceil(metrics->top + metrics->height));
      metrics->left = ClampTo<int32_t>(std::floor(metrics->left));
      metrics->top = ClampTo<int32_t>(std::floor(metrics->top));
      metrics->width = right - metrics->left;
      metrics->height = bottom - metrics->top;
    }
  }
  return measured;
}
PlatformFontMetrics FontPlatformData::GetFontMetrics() const {
  return typeface_->GetFontMetrics(text_size_, RenderOptions());
}
} // namespace bkfont
