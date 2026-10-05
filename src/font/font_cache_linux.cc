// Ported from: blink/renderer/platform/fonts/linux/font_cache_linux.cc
// Ported from: blink/renderer/platform/fonts/skia/font_cache_skia.cc
// Native matching, file identity, fallback order and system font metadata.
// Sized font data and rendering policy are shared in font_cache.cc.

/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "font_cache.h"

#include <cassert>

#include "base/text/character_names.h"
#include "font_description.h"
#include "font_face_creation_params.h"
#include "font_fallback_linux.h"
#include "font_platform_data.h"
#include "font_unique_name_lookup_linux.h"
#include "platform/fontconfig_util.h"
#include "platform/font_manager_fontconfig.h"

namespace bkfont {

std::shared_ptr<FontManager> FontCache::CreateFontManager() {
  return MakeFontManagerFontconfig();
}

std::shared_ptr<const FontPlatformData> FontCache::PlatformLastResortFont(const FontDescription&) {
  return nullptr;
}

bool FontCache::IsFamilyAvailable(const String& family) const {
  return static_cast<bool>(font_manager_->MatchFamilyStyle(family, FontStyle()));
}

namespace {

AtomicString& MutableSystemFontFamily() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(AtomicString, system_font_family, ());
  return system_font_family;
}

} // namespace

const AtomicString& FontCache::SystemFontFamily() {
  return MutableSystemFontFamily();
}

void FontCache::SetSystemFontFamily(const AtomicString& family_name) {
  assert(!family_name.empty());
  MutableSystemFontFamily() = family_name;
}

std::shared_ptr<Typeface> FontCache::MatchTypeface(const FontDescription& description,
                                                 const FontFaceCreationParams& params, AlternateFontName, String& name) {
  if (params.CreationType() == kCreateFontByFciIdAndTtcIndex) {
    // This library runs without Chromium's sandbox service, so the returned
    // file identity is opened directly, including its collection/instance index.
    return font_manager_->MakeFromStream(OpenFontconfigStream(params.Filename()), params.TtcIndex());
  }
  name = params.Family().GetString();
  return font_manager_->MatchFamilyStyle(name.empty() ? String() : name, description.PlatformFontStyle());
}

std::shared_ptr<Typeface> FontCache::CreateTypefaceFromUniqueName(const FontFaceCreationParams& params) {
  return FontUniqueNameLookupLinux::MatchUniqueName(params.Family().GetString());
}

std::shared_ptr<const SimpleFontData> FontCache::PlatformFallbackFontForCharacter(
    const FontDescription& font_description, UChar32 character,
    std::shared_ptr<const SimpleFontData>, FontFallbackPriority fallback_priority) {
  if (IsEmojiPresentationEmoji(fallback_priority)) {
    // Preserve Linux's family-emoji probe for emoji sequences.
    character = uchar::kFamily;
  }
  if (!IsEmojiPresentationEmoji(fallback_priority) &&
      (font_description.Style() == kItalicSlopeValue || font_description.Weight() >= kBoldThreshold)) {
    auto standard_font = FallbackOnStandardFontStyle(font_description, character);
    if (standard_font) return standard_font;
  }

  FallbackFontData fallback_font;
  if (!GetFallbackFontForChar(character,
                             IsEmojiPresentationEmoji(fallback_priority)
                                 ? kColorEmojiLocale
                                 : font_description.LocaleOrDefault().Ascii(),
                             &fallback_font)) {
    return nullptr;
  }
  FontFaceCreationParams creation_params(fallback_font.filepath, fallback_font.fontconfig_interface_id, fallback_font.ttc_index);
  bool synthetic_bold = false;
  bool synthetic_italic = false;
  FontDescription description(font_description);
  if (fallback_font.is_bold && description.Weight() < kBoldThreshold) {
    description.SetWeight(kBoldWeightValue);
  }
  if (!fallback_font.is_bold && description.Weight() >= kBoldThreshold && font_description.SyntheticBoldAllowed()) {
    synthetic_bold = true;
    description.SetWeight(kNormalWeightValue);
  }
  if (fallback_font.is_italic && description.Style() == kNormalSlopeValue) {
    description.SetStyle(kItalicSlopeValue);
  }
  if (!fallback_font.is_italic && description.Style() == kItalicSlopeValue && font_description.SyntheticItalicAllowed()) {
    synthetic_italic = true;
    description.SetStyle(kNormalSlopeValue);
  }
  auto substitute_data = GetFontPlatformData(description, creation_params);
  if (!substitute_data) return nullptr;
  auto platform_data = std::make_shared<FontPlatformData>(*substitute_data);
  platform_data->SetSyntheticBold(synthetic_bold);
  platform_data->SetSyntheticItalic(synthetic_italic);
  return FontDataFromFontPlatformData(platform_data);
}

} // namespace bkfont
