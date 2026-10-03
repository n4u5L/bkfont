/*
 * Copyright (c) 2006, 2007, 2008, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Port source: platform/fonts/font_platform_data.h; DirectWrite replaces the typeface boundary.
#pragma once
#include <cstdint>
#include <memory>
#include "font_orientation.h"
#include "resolved_font_features.h"
#include "text_rendering_mode.h"
#include "typesetting_features.h"
#include "platform/font_face.h"
#include "base/hash_table_deleted_value_type.h"
namespace blink {
class HarfBuzzFace;
class FontDescription;
class FontPlatformData {
public:
  FontPlatformData();
  explicit FontPlatformData(HashTableDeletedValueType);
  FontPlatformData(const FontPlatformData&);
  FontPlatformData(const FontPlatformData&, float text_size);
  FontPlatformData(std::shared_ptr<FontFace>, const String& family, float text_size,
                   bool synthetic_bold, bool synthetic_italic,
                   TextRenderingMode, ResolvedFontFeatures,
                   FontOrientation = FontOrientation::kHorizontal);
  ~FontPlatformData();
  FontPlatformData& operator=(const FontPlatformData&) = delete;
  String FontFamilyName() const;
  String GetPostScriptName() const;
  bool IsAhem() const;
  float size() const {
    return text_size_;
  }
  float Size() const {
    return text_size_;
  }
  FontRenderOptions RenderOptions() const;
  bool MeasureGlyph(uint16_t glyph, PlatformGlyphMetrics* metrics) const;
  PlatformFontMetrics GetFontMetrics() const;
  bool ShouldSubpixelPosition() const {
    return use_anti_alias_;
  }
  bool SyntheticBold() const {
    return synthetic_bold_;
  }
  bool SyntheticItalic() const {
    return synthetic_italic_;
  }
  const std::shared_ptr<FontFace>& GetFontFace() const {
    return typeface_;
  }
  const std::shared_ptr<FontFace>& Face() const {
    return typeface_;
  }
  HarfBuzzFace* GetHarfBuzzFace() const;
  bool HasSpaceInLigaturesOrKerning(TypesettingFeatures) const;
  uint64_t UniqueID() const;
  unsigned GetHash() const;
  FontOrientation Orientation() const {
    return orientation_;
  }
  const ResolvedFontFeatures& ResolvedFeatures() const {
    return resolved_font_features_;
  }
  bool IsVerticalAnyUpright() const {
    return blink::IsVerticalAnyUpright(orientation_);
  }
  bool IsVerticalNonCJKUpright() const {
    return blink::IsVerticalNonCJKUpright(orientation_);
  }
  void SetOrientation(FontOrientation value) {
    orientation_ = value;
  }
  void SetSyntheticBold(bool value) {
    synthetic_bold_ = value;
  }
  void SetSyntheticItalic(bool value) {
    synthetic_italic_ = value;
  }
  void SetAvoidEmbeddedBitmaps(bool value) {
    avoid_embedded_bitmaps_ = value;
  }
  bool operator==(const FontPlatformData&) const;
  bool operator!=(const FontPlatformData& value) const {
    return !(*this == value);
  }
  bool IsHashTableDeletedValue() const {
    return is_hash_table_deleted_value_;
  }
  bool FontContainsCharacter(UChar32) const;

private:
  const std::shared_ptr<FontFace> typeface_;

public:
  float text_size_ = 0;
  bool synthetic_bold_ = false;
  bool synthetic_italic_ = false;
  bool avoid_embedded_bitmaps_ = false;
  TextRenderingMode text_rendering_ = kAutoTextRendering;
  FontOrientation orientation_ = FontOrientation::kHorizontal;
  ResolvedFontFeatures resolved_font_features_;

private:
  // HarfBuzzFace borrows this platform record. No shared ownership back-edge.
  mutable std::unique_ptr<HarfBuzzFace> harfbuzz_face_;
  bool is_hash_table_deleted_value_ = false;
  bool use_anti_alias_ = false;
  bool use_subpixel_rendering_ = false;
};
} // namespace blink
