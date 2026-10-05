// Ported from: chromium/ui/gfx/font_fallback_linux.cc (GetFallbackFontForChar)

// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "font_fallback_linux.h"

#include <map>
#include <memory>
#include <vector>

#include "base/mutex.h"
#include "platform/fontconfig_util.h"

namespace bkfont {

namespace {

class CachedFont {
public:
  CachedFont(FcPattern* pattern, FcCharSet* charset)
      : supported_characters_(charset) {
    const char* family = GetFontconfigString(pattern, FC_FAMILY);
    fallback_font_.name = family ? family : "";
    fallback_font_.filepath = GetFontconfigPath(pattern);
    fallback_font_.ttc_index = GetFontconfigInteger(pattern, FC_INDEX, 0);
    fallback_font_.is_bold = GetFontconfigInteger(pattern, FC_WEIGHT, FC_WEIGHT_NORMAL) >= FC_WEIGHT_BOLD;
    fallback_font_.is_italic = GetFontconfigInteger(pattern, FC_SLANT, FC_SLANT_ROMAN) != FC_SLANT_ROMAN;
  }

  const FallbackFontData& FallbackFont() const {
    return fallback_font_;
  }
  bool HasGlyphForCharacter(std::int32_t character) const {
    return supported_characters_ && FcCharSetHasChar(supported_characters_, character);
  }

private:
  FallbackFontData fallback_font_;
  FcCharSet* supported_characters_; // Owned by CachedFontSet's font_set_.
};

class CachedFontSet {
public:
  explicit CachedFontSet(const std::string& locale) {
    FontconfigPattern pattern(FcPatternCreate());
    if (!locale.empty()) {
      FcPatternAddString(pattern.get(), FC_LANG, reinterpret_cast<const FcChar8*>(locale.c_str()));
    }
    FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue);
    FcConfigSubstitute(GetGlobalFontConfig(), pattern.get(), FcMatchPattern);
    FcDefaultSubstitute(pattern.get());
    if (locale.empty()) FcPatternDel(pattern.get(), FC_LANG);
    FcResult result;
    font_set_.reset(FcFontSort(GetGlobalFontConfig(), pattern.get(), FcFalse, nullptr, &result));
    if (!font_set_) return;
    for (int i = 0; i < font_set_->nfont; ++i) {
      FcPattern* current = font_set_->fonts[i];
      if (!IsValidFallbackFont(current)) continue;
      FcCharSet* charset;
      if (FcPatternGetCharSet(current, FC_CHARSET, 0, &charset) != FcResultMatch) continue;
      fallback_list_.emplace_back(current, charset);
    }
  }

  bool GetFallbackFontForChar(std::int32_t character, FallbackFontData* fallback_font) const {
    for (const auto& font : fallback_list_) {
      if (font.HasGlyphForCharacter(character)) {
        *fallback_font = font.FallbackFont();
        return true;
      }
    }
    return false;
  }

private:
  FontconfigFontSet font_set_;
  std::vector<CachedFont> fallback_list_;
};

} // namespace

bool GetFallbackFontForChar(std::int32_t character, const std::string& locale, FallbackFontData* fallback_font) {
  using FontSetCache = std::map<std::string, std::unique_ptr<CachedFontSet>>;
  static auto& cache = *new FontSetCache;
  static auto& mutex = *new Mutex;
  AutoMutexExclusive cache_lock(mutex);
  FontconfigLocker lock;
  auto& font_set = cache[locale];
  if (!font_set) font_set = std::make_unique<CachedFontSet>(locale);
  return font_set->GetFallbackFontForChar(character, fallback_font);
}

} // namespace bkfont
