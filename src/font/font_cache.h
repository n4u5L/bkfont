/*
 * Copyright (C) 2006, 2008 Apple Computer, Inc.  All rights reserved.
 * Copyright (C) 2007-2008 Torch Mobile, Inc.
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

// Port source: platform/fonts/font_cache.h (Windows/font-management API).
#pragma once
#include "fallback_list_composite_key.h"
#include "font_cache_client.h"
#include "font_data_cache.h"
#include "font_platform_data_cache.h"
#include "font_fallback_priority.h"
#include "platform/font_manager.h"
#include "base/hash_map.h"
namespace blink {
class ShapeCache;
class FontPrewarmer;
class FontFallbackMap;
enum class AlternateFontName {
  kAllowAlternate,
  kNoAlternate,
  kLocalUniqueFace,
  kLastResort
};
extern const char kColorEmojiLocale[];
extern const char kMonoEmojiLocale[];
class FontCache final {
public:
  static FontCache& Get();
  FontCache();
  ~FontCache();
  FontCache(const FontCache&) = delete;
  FontCache& operator=(const FontCache&) = delete;
  std::shared_ptr<const SimpleFontData> GetFontData(const FontDescription&, const AtomicString&, AlternateFontName = AlternateFontName::kAllowAlternate);
  std::shared_ptr<const SimpleFontData> GetLastResortFallbackFont(const FontDescription&);
  std::shared_ptr<const SimpleFontData> FallbackFontForCharacter(const FontDescription&, UChar32, std::shared_ptr<const SimpleFontData>, FontFallbackPriority = FontFallbackPriority::kText);
  std::shared_ptr<const FontPlatformData> GetFontPlatformData(const FontDescription&, const FontFaceCreationParams&, AlternateFontName = AlternateFontName::kAllowAlternate);
  std::shared_ptr<const FontPlatformData> CreateFontPlatformData(const FontDescription&, const FontFaceCreationParams&, float font_size, AlternateFontName);
  std::shared_ptr<const SimpleFontData> FontDataFromFontPlatformData(std::shared_ptr<const FontPlatformData>, bool subpixel_ascent_descent = false);
  bool IsPlatformFamilyMatchAvailable(const FontDescription&, const AtomicString&);
  bool IsPlatformFontUniqueNameMatchAvailable(const FontDescription&, const AtomicString&);
  std::shared_ptr<ShapeCache> GetShapeCache(const FallbackListCompositeKey&);
  FontFallbackMap& GetFontFallbackMap();
  void AddClient(const std::shared_ptr<FontCacheClient>&);
  uint16_t Generation() const {
    return generation_;
  }
  void Invalidate();
  void InvalidateShapeCache();
  static String FirstAvailableOrFirst(const String&);
  static FontPrewarmer* GetFontPrewarmer() {
    return prewarmer_;
  }
  static void SetFontPrewarmer(FontPrewarmer* prewarmer) {
    prewarmer_ = prewarmer;
  }
  static void PrewarmFamily(const AtomicString&);
  static bool AntialiasedTextEnabled() {
    return antialiased_text_enabled_;
  }
  static bool LcdTextEnabled() {
    return lcd_text_enabled_;
  }
  static void SetAntialiasedTextEnabled(bool value) {
    antialiased_text_enabled_ = value;
  }
  static void SetLCDTextEnabled(bool value) {
    lcd_text_enabled_ = value;
  }
  static const AtomicString& SystemFontFamily();
  static void SetMenuFontMetrics(const AtomicString&, int32_t);
  static void SetSmallCaptionFontMetrics(const AtomicString&, int32_t);
  static void SetStatusFontMetrics(const AtomicString&, int32_t);
  static const AtomicString& MenuFontFamily() {
    return *menu_font_family_name_;
  }
  static const AtomicString& SmallCaptionFontFamily() {
    return *small_caption_font_family_name_;
  }
  static const AtomicString& StatusFontFamily() {
    return *status_font_family_name_;
  }
  static int32_t MenuFontHeight() {
    return menu_font_height_;
  }
  static int32_t SmallCaptionFontHeight() {
    return small_caption_font_height_;
  }
  static int32_t StatusFontHeight() {
    return status_font_height_;
  }
  std::shared_ptr<blink::FontManager> GetFontManager() const {
    return font_manager_;
  }
  std::shared_ptr<const SimpleFontData> GetFallbackFamilyNameFromHardcodedChoices(const FontDescription&, UChar32, FontFallbackPriority);
  std::shared_ptr<const SimpleFontData> GetDWriteFallbackFamily(const FontDescription&, UChar32, FontFallbackPriority);

private:
  std::shared_ptr<FontFace> CreateTypeface(const FontDescription&, const FontFaceCreationParams&, String&);
  std::shared_ptr<FontFace> CreateTypefaceFromUniqueName(const FontFaceCreationParams&);
  std::shared_ptr<const SimpleFontData> FallbackOnStandardFontStyle(const FontDescription&, UChar32);
  std::shared_ptr<const SimpleFontData> PlatformFallbackFontForCharacter(const FontDescription&, UChar32, std::shared_ptr<const SimpleFontData>, FontFallbackPriority);
  std::shared_ptr<blink::FontManager> font_manager_;
  FontPlatformDataCache font_platform_data_cache_;
  FontDataCache font_data_cache_;
  HashMap<FallbackListCompositeKey, std::weak_ptr<ShapeCache>, FallbackListCompositeKeyTraits> fallback_list_shaper_cache_;
  // A weak set keyed by the stable client address; expired entries are removed.
  HashMap<FontCacheClient*, std::weak_ptr<FontCacheClient>> font_cache_clients_;
  std::shared_ptr<FontFallbackMap> font_fallback_map_;
  uint16_t generation_ = 0;
  static FontPrewarmer* prewarmer_;
  static bool antialiased_text_enabled_;
  static bool lcd_text_enabled_;
  static std::unique_ptr<AtomicString> menu_font_family_name_;
  static std::unique_ptr<AtomicString> small_caption_font_family_name_;
  static std::unique_ptr<AtomicString> status_font_family_name_;
  static int32_t menu_font_height_;
  static int32_t small_caption_font_height_;
  static int32_t status_font_height_;
};
} // namespace blink
