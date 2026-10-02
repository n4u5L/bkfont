/*
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Nicholas Shanks <webkit@nickshanks.com>
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

// Backend adaptation preserves Blink matching/fallback order.
#include "font_cache.h"
#include "font_fallback_map.h"
#include "font_fallback_win.h"
#include "alternate_font_family.h"
#include "shaping/shape_cache.h"
#include "runtime_enabled_features.h"
#include "text/character.h"
#include <windows.h>
#include "font_prewarmer.h"
#include "wtf/hash_set.h"
#include <unicode/uscript.h>
namespace blink {
const char kColorEmojiLocale[] = "und-Zsye";
const char kMonoEmojiLocale[] = "und-Zsym";
static const char kChineseSimplified[] = "zh-Hant";
FontPrewarmer* FontCache::prewarmer_ = nullptr;
bool FontCache::antialiased_text_enabled_ = false;
bool FontCache::lcd_text_enabled_ = false;
std::unique_ptr<AtomicString> FontCache::menu_font_family_name_;
std::unique_ptr<AtomicString> FontCache::small_caption_font_family_name_;
std::unique_ptr<AtomicString> FontCache::status_font_family_name_;
int32_t FontCache::menu_font_height_ = 0;
int32_t FontCache::small_caption_font_height_ = 0;
int32_t FontCache::status_font_height_ = 0;
FontCache& FontCache::Get() {
  static thread_local FontCache cache;
  return cache;
}
FontCache::FontCache()
    : font_manager_(blink::FontManager::Create()) {
}
FontCache::~FontCache() = default;
std::shared_ptr<const FontPlatformData> FontCache::GetFontPlatformData(const FontDescription& description,
                                                                       const FontFaceCreationParams& params, AlternateFontName alternate) {
  if (params.CreationType() == kCreateFontByFamily && params.Family() == font_family_names::kSystemUi)
    return GetFontPlatformData(description, FontFaceCreationParams(SystemFontFamily()), AlternateFontName::kNoAlternate);
  return font_platform_data_cache_.GetOrCreateFontPlatformData(this, description, params, alternate);
}
std::shared_ptr<FontFace> FontCache::CreateTypeface(const FontDescription& description,
                                                    const FontFaceCreationParams& params, String& name) {
  name = params.Family().GetString();
  return font_manager_->MatchFamily(name, description.PlatformFontStyle());
}
std::shared_ptr<FontFace> FontCache::CreateTypefaceFromUniqueName(const FontFaceCreationParams& params) {
  return font_manager_->MatchUniqueName(params.Family().GetString());
}
std::shared_ptr<ShapeCache> FontCache::GetShapeCache(const FallbackListCompositeKey& key) {
  // Upstream Oilpan weak processing removed unreachable ShapeCache entries.
  fallback_list_shaper_cache_.erase_if([](const auto& entry) { return entry.value.expired(); });
  auto result = fallback_list_shaper_cache_.insert(key, std::weak_ptr<ShapeCache>());
  if (auto cache = result.stored_value->value.lock()) return cache;
  auto cache = std::make_shared<ShapeCache>();
  result.stored_value->value = cache;
  return cache;
}
void FontCache::AddClient(const std::shared_ptr<FontCacheClient>& client) {
  font_cache_clients_.erase_if([](const auto& entry) { return entry.value.expired(); });
  font_cache_clients_.insert(client.get(), std::weak_ptr<FontCacheClient>(client));
}
void FontCache::InvalidateShapeCache() {
  fallback_list_shaper_cache_.erase_if([](const auto& entry) { return entry.value.expired(); });
  for (auto& entry : fallback_list_shaper_cache_) {
    if (auto cache = entry.value.lock()) cache->Clear();
  }
}
void FontCache::Invalidate() {
  font_platform_data_cache_.Clear();
  font_data_cache_.Clear();
  ++generation_;
  // GC cleared dead clients upstream; weak clients are discarded on access here.
  font_cache_clients_.erase_if([](const auto& entry) { return entry.value.expired(); });
  for (auto& entry : font_cache_clients_)
    if (auto client = entry.value.lock()) client->FontCacheInvalidated();
  InvalidateShapeCache();
}
FontFallbackMap& FontCache::GetFontFallbackMap() {
  if (!font_fallback_map_) {
    font_fallback_map_ = std::make_shared<FontFallbackMap>(nullptr);
    AddClient(font_fallback_map_);
  }
  return *font_fallback_map_;
}
// Port source: third_party/blink/renderer/platform/fonts/font_cache.cc + win/font_cache_skia_win.cc + skia/font_cache_skia.cc
int32_t EnsureMinimumFontHeightIfNeeded(int32_t font_height) {
  // Adjustment for codepage 936 to make the fonts more legible in Simplified
  // Chinese.  Please refer to LayoutThemeFontProviderWin.cpp for more
  // information.
  return ((font_height < 12.0f) && (GetACP() == 936)) ? 12.0f : font_height;
}

const LayoutLocale* FallbackLocaleForCharacter(
    const FontDescription& font_description,
    const FontFallbackPriority& fallback_priority,
    const UChar32 codepoint) {
  if (IsEmojiPresentationEmoji(fallback_priority)) {
    return LayoutLocale::Get(AtomicString(kColorEmojiLocale));
  } else if (RuntimeEnabledFeatures::SystemFallbackEmojiVSSupportEnabled() && IsTextPresentationEmoji(fallback_priority)) {
    return LayoutLocale::Get(AtomicString(kMonoEmojiLocale));
  }

  UErrorCode error_code = U_ZERO_ERROR;
  const UScriptCode char_script = uscript_getScript(codepoint, &error_code);
  if (U_SUCCESS(error_code) && char_script == USCRIPT_HAN) {
    // If we were unable to disambiguate the requested Han ideograph from the
    // content locale, the Accept-Language headers or system locale, assume it's
    // simplified Chinese. It's important to pass a CJK locale to the fallback
    // call in order to avoid priming the browser side cache incorrectly with an
    // ambiguous locale for Han fallback requests.
    const LayoutLocale* han_locale =
        LayoutLocale::LocaleForHan(font_description.Locale());
    return han_locale ? han_locale
                      : LayoutLocale::Get(AtomicString(kChineseSimplified));
  }

  return font_description.Locale() ? font_description.Locale()
                                   : &LayoutLocale::GetDefault();
}

const AtomicString& FontCache::SystemFontFamily() {
  return MenuFontFamily();
}

void FontCache::SetMenuFontMetrics(const AtomicString& family_name,
                                   int32_t font_height) {
  menu_font_family_name_ = std::make_unique<AtomicString>(family_name);
  menu_font_height_ = EnsureMinimumFontHeightIfNeeded(font_height);
}

void FontCache::SetSmallCaptionFontMetrics(const AtomicString& family_name,
                                           int32_t font_height) {
  small_caption_font_family_name_ = std::make_unique<AtomicString>(family_name);
  small_caption_font_height_ = EnsureMinimumFontHeightIfNeeded(font_height);
}

void FontCache::SetStatusFontMetrics(const AtomicString& family_name,
                                     int32_t font_height) {
  status_font_family_name_ = std::make_unique<AtomicString>(family_name);
  status_font_height_ = EnsureMinimumFontHeightIfNeeded(font_height);
}

std::shared_ptr<const SimpleFontData> FontCache::GetFallbackFamilyNameFromHardcodedChoices(
    const FontDescription& font_description,
    UChar32 codepoint,
    FontFallbackPriority fallback_priority) {
  UScriptCode script;

  if (const AtomicString fallback_family =
          GetFallbackFamily(codepoint, font_description.GenericFamily(), font_description.Locale(), fallback_priority, *font_manager_, script)) {
    FontFaceCreationParams create_by_family =
        FontFaceCreationParams(fallback_family);
    std::shared_ptr<const FontPlatformData> data =
        GetFontPlatformData(font_description, create_by_family);
    if (data && data->FontContainsCharacter(codepoint)) {
      return FontDataFromFontPlatformData(data);
    }
  }

  // If instantiating the returned fallback family was not successful, probe for
  // a set of potential fonts with wide coverage.

  // Last resort font list : PanUnicode. CJK fonts have a pretty
  // large repertoire. Eventually, we need to scan all the fonts
  // on the system to have a Firefox-like coverage.
  // Make sure that all of them are lowercased.
  const static UChar* const kCjkFonts[] = {
      u"arial unicode ms",
      u"ms pgothic",
      u"simsun",
      u"gulim",
      u"pmingliu",
      u"wenquanyi zen hei", // Partial CJK Ext. A coverage but more widely
                            // known to Chinese users.
      u"ar pl shanheisun uni",
      u"ar pl zenkai uni",
      u"han nom a", // Complete CJK Ext. A coverage.
      u"code2000"   // Complete CJK Ext. A coverage.
                    // CJK Ext. B fonts are not listed here because it's of no use
                    // with our current non-BMP character handling because we use
                    // Uniscribe for it and that code path does not go through here.
  };

  const static UChar* const kCommonFonts[] = {
      u"tahoma",
      u"arial unicode ms",
      u"lucida sans unicode",
      u"microsoft sans serif",
      u"palatino linotype",
      // Six fonts below (and code2000 at the end) are not from MS, but
      // once installed, cover a very wide range of characters.
      u"dejavu serif",
      u"dejavu sasns",
      u"freeserif",
      u"freesans",
      u"gentium",
      u"gentiumalt",
      u"ms pgothic",
      u"simsun",
      u"gulim",
      u"pmingliu",
      u"code2000"};

  const UChar* const* pan_uni_fonts = nullptr;
  int num_fonts = 0;
  if (script == USCRIPT_HAN) {
    pan_uni_fonts = kCjkFonts;
    num_fonts = std::size(kCjkFonts);
  } else {
    pan_uni_fonts = kCommonFonts;
    num_fonts = std::size(kCommonFonts);
  }
  // Font returned from getFallbackFamily may not cover |character|
  // because it's based on script to font mapping. This problem is
  // critical enough for non-Latin scripts (especially Han) to
  // warrant an additional (real coverage) check with fontCotainsCharacter.
  for (int i = 0; i < num_fonts; ++i) {
    FontFaceCreationParams create_by_family =
        FontFaceCreationParams(AtomicString(pan_uni_fonts[i]));
    std::shared_ptr<const FontPlatformData> data =
        GetFontPlatformData(font_description, create_by_family);
    if (data && data->FontContainsCharacter(codepoint))
      return FontDataFromFontPlatformData(data);
  }
  return nullptr;
}

std::shared_ptr<const SimpleFontData> FontCache::GetDWriteFallbackFamily(const FontDescription& description,
                                                                         UChar32 codepoint, FontFallbackPriority priority) {
  const LayoutLocale* locale = FallbackLocaleForCharacter(description, priority, codepoint);
  auto face = font_manager_->MatchCharacter(description.Family().FamilyName().GetString(),
                                            description.PlatformFontStyle(),
                                            locale->LocaleString().GetString(),
                                            codepoint);
  if (!face) return nullptr;
  FontDescription fallback_description(description);
  fallback_description.UpdateFromPlatformFontStyle(face->Style());
  const FontFaceCreationParams params(AtomicString(face->FamilyName()));
  auto data = GetFontPlatformData(fallback_description, params);
  if (!data || !data->FontContainsCharacter(codepoint)) return nullptr;
  return FontDataFromFontPlatformData(data);
}

std::shared_ptr<const SimpleFontData> FontCache::PlatformFallbackFontForCharacter(
    const FontDescription& font_description,
    UChar32 character,
    std::shared_ptr<const SimpleFontData> original_font_data,
    FontFallbackPriority fallback_priority) {

  // First try the specified font with standard style & weight.
  if (!IsEmojiPresentationEmoji(fallback_priority) && (font_description.Style() == kItalicSlopeValue || font_description.Weight() >= kBoldWeightValue)) {
    std::shared_ptr<const SimpleFontData> font_data =
        FallbackOnStandardFontStyle(font_description, character);
    if (font_data)
      return font_data;
  }

  FontFallbackPriority fallback_priority_with_emoji_text = fallback_priority;
  if (RuntimeEnabledFeatures::SystemFallbackEmojiVSSupportEnabled() && fallback_priority == FontFallbackPriority::kText && Character::IsEmoji(character)) {
    fallback_priority_with_emoji_text = FontFallbackPriority::kEmojiText;
  }

  std::shared_ptr<const SimpleFontData> hardcoded_list_fallback_font =
      GetFallbackFamilyNameFromHardcodedChoices(
          font_description,
          character,
          fallback_priority_with_emoji_text);

  // Fall through to running the API-based fallback.
  if (!hardcoded_list_fallback_font) {
    return GetDWriteFallbackFamily(font_description, character, fallback_priority_with_emoji_text);
  }

  return hardcoded_list_fallback_font;
}

static bool TypefacesMatchesFamily(const FontFace* face, const AtomicString& family) {
  for (const auto& actual : face->FamilyNames())
    if (DeprecatedEqualIgnoringCase(family, actual.name)) return true;
  return DeprecatedEqualIgnoringCase(family, face->FamilyName());
}

static bool TypefacesHasWeightSuffix(const AtomicString& family,
                                     AtomicString& adjusted_name,
                                     FontSelectionValue& variant_weight) {
  struct FamilyWeightSuffix {
    const UChar* suffix;
    wtf_size_t length;
    FontSelectionValue weight;
  };
  // Mapping from suffix to weight from the DirectWrite documentation.
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd368082.aspx
  const static FamilyWeightSuffix kVariantForSuffix[] = {
      {u" thin", 5, FontSelectionValue(100)},
      {u" extralight", 11, FontSelectionValue(200)},
      {u" ultralight", 11, FontSelectionValue(200)},
      {u" light", 6, FontSelectionValue(300)},
      {u" regular", 8, FontSelectionValue(400)},
      {u" medium", 7, FontSelectionValue(500)},
      {u" demibold", 9, FontSelectionValue(600)},
      {u" semibold", 9, FontSelectionValue(600)},
      {u" extrabold", 10, FontSelectionValue(800)},
      {u" ultrabold", 10, FontSelectionValue(800)},
      {u" black", 6, FontSelectionValue(900)},
      {u" heavy", 6, FontSelectionValue(900)}};
  size_t num_variants = std::size(kVariantForSuffix);
  for (size_t i = 0; i < num_variants; i++) {
    const FamilyWeightSuffix& entry = kVariantForSuffix[i];
    if (family.DeprecatedEndsWithIgnoringCase(entry.suffix)) {
      String family_name = family.GetString();
      family_name.Truncate(family.length() - entry.length);
      adjusted_name = AtomicString(family_name);
      variant_weight = entry.weight;
      return true;
    }
  }

  return false;
}

static bool TypefacesHasStretchSuffix(const AtomicString& family,
                                      AtomicString& adjusted_name,
                                      FontSelectionValue& variant_stretch) {
  struct FamilyStretchSuffix {
    const UChar* suffix;
    wtf_size_t length;
    FontSelectionValue stretch;
  };
  // Mapping from suffix to stretch value from the DirectWrite documentation.
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd368078.aspx
  // Also includes Narrow as a synonym for Condensed to to support Arial
  // Narrow and other fonts following the same naming scheme.
  const static FamilyStretchSuffix kVariantForSuffix[] = {
      {u" ultracondensed", 15, kUltraCondensedWidthValue},
      {u" extracondensed", 15, kExtraCondensedWidthValue},
      {u" condensed", 10, kCondensedWidthValue},
      {u" narrow", 7, kCondensedWidthValue},
      {u" semicondensed", 14, kSemiCondensedWidthValue},
      {u" semiexpanded", 13, kSemiExpandedWidthValue},
      {u" expanded", 9, kExpandedWidthValue},
      {u" extraexpanded", 14, kExtraExpandedWidthValue},
      {u" ultraexpanded", 14, kUltraExpandedWidthValue}};
  size_t num_variants = std::size(kVariantForSuffix);
  for (size_t i = 0; i < num_variants; i++) {
    const FamilyStretchSuffix& entry = kVariantForSuffix[i];
    if (family.DeprecatedEndsWithIgnoringCase(entry.suffix)) {
      String family_name = family.GetString();
      family_name.Truncate(family.length() - entry.length);
      adjusted_name = AtomicString(family_name);
      variant_stretch = entry.stretch;
      return true;
    }
  }

  return false;
}

std::shared_ptr<const FontPlatformData> FontCache::CreateFontPlatformData(
    const FontDescription& font_description,
    const FontFaceCreationParams& creation_params,
    float font_size,
    AlternateFontName alternate_font_name) {

  std::shared_ptr<FontFace> typeface;

  String name;

  if (alternate_font_name == AlternateFontName::kLocalUniqueFace && RuntimeEnabledFeatures::FontSrcLocalMatchingEnabled()) {
    typeface = CreateTypefaceFromUniqueName(creation_params);

    // We do not need to try any heuristic around the font name, as below, for
    // family matching.
    if (!typeface)
      return nullptr;

  } else {
    typeface = CreateTypeface(font_description, creation_params, name);

    // For a family match, Windows will always give us a valid pointer here,
    // even if the face name is non-existent. We have to double-check and see if
    // the family name was really used.
    if (!typeface || !TypefacesMatchesFamily(typeface.get(), creation_params.Family())) {
      AtomicString adjusted_name;
      FontSelectionValue variant_weight;
      FontSelectionValue variant_stretch;

      // TODO: crbug.com/627143 LocalFontFaceSource.cpp, which implements
      // retrieving src: local() font data uses getFontData, which in turn comes
      // here, to retrieve fonts from the cache and specifies the argument to
      // local() as family name. So we do not match by full font name or
      // postscript name as the spec says:
      // https://drafts.csswg.org/css-fonts-3/#src-desc

      // Prevent one side effect of the suffix translation below where when
      // matching local("Roboto Regular") it tries to find the closest match
      // even though that can be a bold font in case of Roboto Bold.
      if (alternate_font_name == AlternateFontName::kLocalUniqueFace) {
        return nullptr;
      }

      if (alternate_font_name == AlternateFontName::kLastResort) {
        if (!typeface)
          return nullptr;
      } else if (TypefacesHasWeightSuffix(creation_params.Family(),
                                          adjusted_name,
                                          variant_weight)) {
        FontFaceCreationParams adjusted_params(adjusted_name);
        FontDescription adjusted_font_description = font_description;
        adjusted_font_description.SetWeight(variant_weight);
        typeface =
            CreateTypeface(adjusted_font_description, adjusted_params, name);
        if (!typeface || !TypefacesMatchesFamily(typeface.get(), adjusted_name)) {
          return nullptr;
        }

      } else if (TypefacesHasStretchSuffix(creation_params.Family(),
                                           adjusted_name,
                                           variant_stretch)) {
        FontFaceCreationParams adjusted_params(adjusted_name);
        FontDescription adjusted_font_description = font_description;
        adjusted_font_description.SetStretch(variant_stretch);
        typeface =
            CreateTypeface(adjusted_font_description, adjusted_params, name);
        if (!typeface || !TypefacesMatchesFamily(typeface.get(), adjusted_name)) {
          return nullptr;
        }
      } else {
        return nullptr;
      }
    }
  }

  bool synthetic_bold_requested =
      (font_description.Weight() >= kBoldThreshold && typeface->Style().weight < kBoldThreshold) || font_description.IsSyntheticBold();

  bool synthetic_italic_requested =
      ((font_description.Style() == kItalicSlopeValue) && typeface->Style().slant == FontSlant::kNormal) || font_description.IsSyntheticItalic();

  std::shared_ptr<FontPlatformData> result = std::make_shared<FontPlatformData>(
      typeface,
      name,
      font_size,
      synthetic_bold_requested && font_description.SyntheticBoldAllowed(),
      synthetic_italic_requested && font_description.SyntheticItalicAllowed(),
      font_description.TextRendering(),
      ResolvedFontFeatures(),
      font_description.Orientation());

  result->SetAvoidEmbeddedBitmaps(
      typeface->FamilyName() == "Calibri" || typeface->FamilyName() == "Courier New");

  return result;
}

std::shared_ptr<const SimpleFontData> FontCache::FallbackOnStandardFontStyle(
    const FontDescription& font_description,
    UChar32 character) {
  FontDescription substitute_description(font_description);
  substitute_description.SetStyle(kNormalSlopeValue);
  substitute_description.SetWeight(kNormalWeightValue);

  FontFaceCreationParams creation_params(
      substitute_description.Family().FamilyName());
  std::shared_ptr<const FontPlatformData> substitute_platform_data =
      GetFontPlatformData(substitute_description, creation_params);
  if (substitute_platform_data && substitute_platform_data->FontContainsCharacter(character)) {
    std::shared_ptr<FontPlatformData> platform_data =
        std::make_shared<FontPlatformData>(*substitute_platform_data);
    platform_data->SetSyntheticBold(font_description.Weight() >= kBoldThreshold && font_description.SyntheticBoldAllowed());
    platform_data->SetSyntheticItalic(
        font_description.Style() == kItalicSlopeValue && font_description.SyntheticItalicAllowed());
    return FontDataFromFontPlatformData(platform_data);
  }

  return nullptr;
}

std::shared_ptr<const SimpleFontData> FontCache::GetLastResortFallbackFont(
    const FontDescription& description) {
  const FontFaceCreationParams fallback_creation_params(
      GetFallbackFontFamily(description));
  std::shared_ptr<const FontPlatformData> font_platform_data = GetFontPlatformData(
      description,
      fallback_creation_params,
      AlternateFontName::kLastResort);

  // We should at least have Sans or Arial which is the last resort fallback of
  // SkFontHost ports.
  if (!font_platform_data) {
    static const FontFaceCreationParams sans_creation_params(font_family_names::kSans);
    font_platform_data = GetFontPlatformData(description, sans_creation_params, AlternateFontName::kLastResort);
  }
  if (!font_platform_data) {
    static const FontFaceCreationParams arial_creation_params(font_family_names::kArial);
    font_platform_data = GetFontPlatformData(description, arial_creation_params, AlternateFontName::kLastResort);
  }
#if BUILDFLAG(IS_WIN)
  // Try some more Windows-specific fallbacks.
  if (!font_platform_data) {
    static const FontFaceCreationParams msuigothic_creation_params(font_family_names::kMSUIGothic);
    font_platform_data =
        GetFontPlatformData(description, msuigothic_creation_params, AlternateFontName::kLastResort);
  }
  if (!font_platform_data) {
    static const FontFaceCreationParams mssansserif_creation_params(font_family_names::kMicrosoftSansSerif);
    font_platform_data =
        GetFontPlatformData(description, mssansserif_creation_params, AlternateFontName::kLastResort);
  }
  if (!font_platform_data) {
    static const FontFaceCreationParams segoeui_creation_params(font_family_names::kSegoeUI);
    font_platform_data = GetFontPlatformData(
        description,
        segoeui_creation_params,
        AlternateFontName::kLastResort);
  }
  if (!font_platform_data) {
    static const FontFaceCreationParams calibri_creation_params(font_family_names::kCalibri);
    font_platform_data = GetFontPlatformData(
        description,
        calibri_creation_params,
        AlternateFontName::kLastResort);
  }
  if (!font_platform_data) {
    static const FontFaceCreationParams timesnewroman_creation_params(font_family_names::kTimesNewRoman);
    font_platform_data =
        GetFontPlatformData(description, timesnewroman_creation_params, AlternateFontName::kLastResort);
  }
  if (!font_platform_data) {
    static const FontFaceCreationParams couriernew_creation_params(font_family_names::kCourierNew);
    font_platform_data =
        GetFontPlatformData(description, couriernew_creation_params, AlternateFontName::kLastResort);
  }
#endif

  return FontDataFromFontPlatformData(font_platform_data);
}

std::shared_ptr<const SimpleFontData> FontCache::GetFontData(
    const FontDescription& font_description,
    const AtomicString& family,
    AlternateFontName altername_font_name) {
  if (std::shared_ptr<const FontPlatformData> platform_data = GetFontPlatformData(
          font_description,
          FontFaceCreationParams(
              AdjustFamilyNameToAvoidUnsupportedFonts(family)),
          altername_font_name)) {
    return FontDataFromFontPlatformData(
        platform_data,
        font_description.SubpixelAscentDescent());
  }

  return nullptr;
}

std::shared_ptr<const SimpleFontData> FontCache::FontDataFromFontPlatformData(
    std::shared_ptr<const FontPlatformData> platform_data,
    bool subpixel_ascent_descent) {
  return font_data_cache_.Get(platform_data, subpixel_ascent_descent);
}

std::shared_ptr<const SimpleFontData> FontCache::FallbackFontForCharacter(
    const FontDescription& description,
    UChar32 lookup_char,
    std::shared_ptr<const SimpleFontData> font_data_to_substitute,
    FontFallbackPriority fallback_priority) {

  // In addition to PUA, do not perform fallback for non-characters either. Some
  // of these are sentinel characters to detect encodings and do appear on
  // websites. More details on
  // http://www.unicode.org/faq/private_use.html#nonchar1 - See also
  // crbug.com/862352 where performing fallback for U+FFFE causes a memory
  // regression.
  if (Character::IsPrivateUse(lookup_char) || Character::IsNonCharacter(lookup_char))
    return nullptr;
  std::shared_ptr<const SimpleFontData> result = PlatformFallbackFontForCharacter(
      description,
      lookup_char,
      font_data_to_substitute,
      fallback_priority);
  return result;
}

bool FontCache::IsPlatformFamilyMatchAvailable(
    const FontDescription& font_description,
    const AtomicString& family) {
  return static_cast<bool>(GetFontPlatformData(
      font_description,
      FontFaceCreationParams(AdjustFamilyNameToAvoidUnsupportedFonts(family)),
      AlternateFontName::kNoAlternate));
}

bool FontCache::IsPlatformFontUniqueNameMatchAvailable(
    const FontDescription& font_description,
    const AtomicString& unique_font_name) {
  // Return early to avoid attempting fallback.
  if (unique_font_name.empty()) {
    return false;
  }

  return static_cast<bool>(GetFontPlatformData(font_description,
                                               FontFaceCreationParams(unique_font_name),
                                               AlternateFontName::kLocalUniqueFace));
}

} // namespace blink

namespace blink {
void FontCache::PrewarmFamily(const AtomicString& family) {
  if (!prewarmer_) return;
  static HashSet<AtomicString> prewarmed_families;
  const auto result = prewarmed_families.insert(family);
  if (!result.is_new_entry) return;
  prewarmer_->PrewarmFamily(family.GetString());
}
} // namespace blink

namespace blink {
// Source: ui/gfx/font_list.cc::FirstAvailableOrFirst. The source trims only
// ASCII whitespace and drops empty comma-separated entries before matching.
String FontCache::FirstAvailableOrFirst(const String& font_name_list) {
  Vector<String> families;
  unsigned start = 0;
  const auto is_whitespace = [](UChar c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
  };
  for (unsigned end = 0; end <= font_name_list.length(); ++end) {
    if (end != font_name_list.length() && font_name_list[end] != ',') continue;
    unsigned first = start;
    unsigned last = end;
    while (first < last && is_whitespace(font_name_list[first])) ++first;
    while (last > first && is_whitespace(font_name_list[last - 1])) --last;
    if (first != last) families.push_back(font_name_list.Substring(first, last - first));
    start = end + 1;
  }
  if (families.empty()) return g_empty_string;
  if (families.size() == 1) return families[0];
  for (const auto& family : families) {
    if (Get().GetFontManager()->MatchFamily(family, FontStyle())) return family;
  }
  return families[0];
}
} // namespace blink
