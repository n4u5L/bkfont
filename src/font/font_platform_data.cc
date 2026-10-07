// Ported from: blink/renderer/platform/fonts/font_platform_data.cc
// (Linux branch)

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


#include "font_platform_data.h"

#include <cstring>
#include <utility>

#include "font_cache.h"
#include "runtime_enabled_features.h"
#include "shaping/harfbuzz_face.h"

namespace bkit {

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
      style_(source.style_) {
}

FontPlatformData::FontPlatformData(const FontPlatformData& src, float text_size)
    : FontPlatformData(src.typeface_, String(), text_size, src.synthetic_bold_,
                       src.synthetic_italic_, src.text_rendering_, src.resolved_font_features_, src.orientation_) {
}

FontPlatformData::FontPlatformData(std::shared_ptr<bkit::Typeface> typeface,
                                   const String&,
                                   float text_size,
                                   bool synthetic_bold,
                                   bool synthetic_italic,
                                   TextRenderingMode text_rendering,
                                   ResolvedFontFeatures resolved_font_features,
                                   FontOrientation orientation)
    : typeface_(std::move(typeface)),
      text_size_(text_size),
      synthetic_bold_(synthetic_bold),
      synthetic_italic_(synthetic_italic),
      text_rendering_(text_rendering),
      orientation_(orientation),
      resolved_font_features_(std::move(resolved_font_features)),
      style_(FontRenderStyle::Resolve(FontCache::DeviceScaleFactor(), text_rendering)) {
}

FontPlatformData::~FontPlatformData() = default;

bool FontPlatformData::operator==(const FontPlatformData& a) const {
  const bool equal_faces = (!typeface_ || !a.typeface_)
                               ? typeface_ == a.typeface_
                               : bkit::Typeface::Equal(typeface_.get(), a.typeface_.get());
  return equal_faces && text_size_ == a.text_size_ &&
         is_hash_table_deleted_value_ == a.is_hash_table_deleted_value_ &&
         synthetic_bold_ == a.synthetic_bold_ &&
         synthetic_italic_ == a.synthetic_italic_ &&
         avoid_embedded_bitmaps_ == a.avoid_embedded_bitmaps_ &&
         text_rendering_ == a.text_rendering_ &&
         resolved_font_features_ == a.resolved_font_features_ &&
         style_ == a.style_ && orientation_ == a.orientation_;
}

std::uint32_t FontPlatformData::UniqueID() const {
  return typeface_->UniqueID();
}

String FontPlatformData::FontFamilyName() const {
  auto font_family_iterator = typeface_->CreateFamilyNameIterator();
  bkit::Typeface::LocalizedString localized_string{String(""), String("")};
  while (font_family_iterator->Next(&localized_string) &&
         localized_string.string.empty()) {
  }
  // Converting an empty SkString upstream produces a non-null empty String.
  return localized_string.string.IsNull() ? String("") : localized_string.string;
}

String FontPlatformData::GetPostScriptName() const {
  if (!typeface_) {
    return String();
  }
  String postscript_name("");
  bool success = typeface_->GetPostScriptName(&postscript_name);
  return success ? postscript_name : String();
}

bool FontPlatformData::IsAhem() const {
  return FontFamilyName() == "Ahem";
}

HarfBuzzFace* FontPlatformData::GetHarfBuzzFace() const {
  if (!harfbuzz_face_) {
    harfbuzz_face_ = std::make_unique<HarfBuzzFace>(this, UniqueID());
  }
  return harfbuzz_face_.get();
}

bool FontPlatformData::HasSpaceInLigaturesOrKerning(TypesettingFeatures features) const {
  HarfBuzzFace* face = GetHarfBuzzFace();
  return face && face->HasSpaceInLigaturesOrKerning(features);
}

unsigned FontPlatformData::GetHash() const {
  unsigned h = UniqueID();
  h ^= 0x01010101 * ((static_cast<int>(is_hash_table_deleted_value_) << 3) |
                     (static_cast<int>(orientation_) << 2) |
                     (static_cast<int>(synthetic_bold_) << 1) |
                     static_cast<int>(synthetic_italic_));
  std::uint32_t size_bytes;
  std::memcpy(&size_bytes, &text_size_, sizeof(size_bytes));
  return h ^ size_bytes;
}

bool FontPlatformData::FontContainsCharacter(UChar32 character) const {
  return CreatePlatformFont().UnicharToGlyph(character);
}

PlatformFont FontPlatformData::CreatePlatformFont(const FontDescription*) const {
  PlatformFont font(typeface_);
  style_.ApplyToPlatformFont(&font);

  const float text_size = text_size_ >= 0 ? text_size_ : 12;
  font.SetSize(text_size);
  font.SetEmbolden(synthetic_bold_);
  font.SetSkewX(synthetic_italic_ ? -1.0f / 4 : 0);
  font.SetEmbeddedBitmaps(!avoid_embedded_bitmaps_);

  // WebTestSupport's antialiasing override is outside this port.
  if (RuntimeEnabledFeatures::NoFontAntialiasingEnabled()) {
    font.SetEdging(PlatformFont::Edging::kAlias);
  }
  return font;
}

} // namespace bkit
