// Ported from: blink/renderer/platform/fonts/font_cache.cc
// Ported from: blink/renderer/platform/fonts/skia/font_cache_skia.cc
// Ported from: chromium/ui/gfx/font_list.cc

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

// Shared cache, render-data construction and invalidation. Native matching
// and fallback policy are implemented in font_cache_win.cc/font_cache_linux.cc.
#include "font_cache.h"

#include <cmath>

#include "font_description.h"
#include "font_platform_data.h"
#include "font_fallback_map.h"
#include "alternate_font_family.h"
#include "shaping/shape_cache.h"
#include "runtime_enabled_features.h"
#include "text/character.h"
#include "font_prewarmer.h"
#include "base/hash_set.h"
namespace bkit {

const char kColorEmojiLocale[] = "und-Zsye";
const char kMonoEmojiLocale[] = "und-Zsym";
FontPrewarmer* FontCache::prewarmer_ = nullptr;
float FontCache::device_scale_factor_ = 1.0f;
FontCache& FontCache::Get() {
  static thread_local FontCache cache;
  return cache;
}
FontCache::FontCache()
    : font_manager_(CreateFontManager()),
      invalidated_device_scale_factor_(DeviceScaleFactor()) {
}
FontCache::~FontCache() {
  for (auto& entry : font_cache_clients_) {
    auto& caches = entry.key->font_caches_;
    caches.EraseAt(caches.Find(this));
  }
}
std::shared_ptr<const FontPlatformData> FontCache::GetFontPlatformData(const FontDescription& description,
                                                                       const FontFaceCreationParams& params, AlternateFontName alternate) {
  if (params.CreationType() == kCreateFontByFamily && params.Family() == font_family_names::kSystemUi)
    return GetFontPlatformData(description, FontFaceCreationParams(SystemFontFamily()), AlternateFontName::kNoAlternate);
  return font_platform_data_cache_.GetOrCreateFontPlatformData(this, description, params, alternate);
}
std::shared_ptr<const FontPlatformData> FontCache::CreateFontPlatformData(
    const FontDescription& description, const FontFaceCreationParams& params,
    float font_size, AlternateFontName alternate_name) {
  String name;
  std::shared_ptr<Typeface> typeface;
  if (RuntimeEnabledFeatures::FontSrcLocalMatchingEnabled() && alternate_name == AlternateFontName::kLocalUniqueFace) {
    typeface = CreateTypefaceFromUniqueName(params);
  } else {
    typeface = MatchTypeface(description, params, alternate_name, name);
  }
  if (!typeface) return nullptr;

  // Linux/FreeType policy after native matching. Keep synthesis, OpenType
  // features and bitmap exclusions identical for DirectWrite and Fontconfig.
  const bool synthetic_bold =
      (description.Weight() > FontSelectionValue(200) + FontSelectionValue(typeface->GetFontStyle().GetWeight()) ||
       description.IsSyntheticBold()) &&
      description.GetFontSynthesisWeight() == FontDescription::kAutoFontSynthesisWeight;
  const bool synthetic_italic =
      ((description.Style() == kItalicSlopeValue && !typeface->IsItalic()) || description.IsSyntheticItalic()) &&
      description.GetFontSynthesisStyle() == FontDescription::kAutoFontSynthesisStyle;
  auto result = std::make_shared<FontPlatformData>(
      typeface, name, font_size, synthetic_bold, synthetic_italic,
      description.TextRendering(), description.ResolveFontFeatures(), description.Orientation());
  result->SetAvoidEmbeddedBitmaps(typeface->GetFamilyName() == "Calibri" || typeface->GetFamilyName() == "Courier New");
  return result;
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
void FontCache::AddClient(FontCacheClient* client) {
  if (!client || font_cache_clients_.Contains(client)) {
    return;
  }
  font_cache_clients_.insert(client, ++next_client_id_);
  client->font_caches_.push_back(this);
}
void FontCache::RemoveClient(FontCacheClient* client) {
  if (!client || !font_cache_clients_.Contains(client)) {
    return;
  }
  font_cache_clients_.erase(client);
  auto& caches = client->font_caches_;
  caches.EraseAt(caches.Find(this));
}
void FontCache::InvalidateShapeCache() {
  fallback_list_shaper_cache_.erase_if([](const auto& entry) { return entry.value.expired(); });
  for (auto& entry : fallback_list_shaper_cache_) {
    if (auto cache = entry.value.lock()) cache->Clear();
  }
}
bool FontCache::UpdateDeviceScaleFactor(float device_scale_factor) {
  if (!std::isfinite(device_scale_factor) || device_scale_factor <= 0) return false;
  FontCache& cache = Get();
  const bool changed = DeviceScaleFactor() != device_scale_factor;
  SetDeviceScaleFactor(device_scale_factor);
  // Another serialized font thread, or the low-level setter, may already have
  // updated the process-wide value while this cache still holds the old style.
  if (!changed && cache.invalidated_device_scale_factor_ == device_scale_factor) return false;
  cache.Invalidate();
  return true;
}
void FontCache::Invalidate() {
  invalidated_device_scale_factor_ = DeviceScaleFactor();
  font_platform_data_cache_.Clear();
  font_data_cache_.Clear();
  ++generation_;
  // A callback may destroy, unregister, or add clients. Snapshot registration
  // IDs and recheck them without keeping iterators alive across callbacks.
  Vector<std::pair<FontCacheClient*, std::uint64_t>> clients;
  clients.ReserveInitialCapacity(font_cache_clients_.size());
  for (const auto& entry : font_cache_clients_) {
    clients.emplace_back(entry.key, entry.value);
  }
  for (const auto& [client, registration_id] : clients) {
    bool registered;
    {
      auto it = font_cache_clients_.find(client);
      registered = it != font_cache_clients_.end() && it->value == registration_id;
    }
    if (registered) {
      client->FontCacheInvalidated();
    }
  }
  InvalidateShapeCache();
}
FontFallbackMap& FontCache::GetFontFallbackMap() {
  if (!font_fallback_map_) {
    font_fallback_map_ = std::make_unique<FontFallbackMap>(nullptr);
    AddClient(font_fallback_map_.get());
  }
  return *font_fallback_map_;
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
  if (!font_platform_data) {
    font_platform_data = PlatformLastResortFont(description);
  }

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

} // namespace bkit

namespace bkit {

void FontCache::PrewarmFamily(const AtomicString& family) {
  if (!prewarmer_) return;
  static HashSet<AtomicString> prewarmed_families;
  const auto result = prewarmed_families.insert(family);
  if (!result.is_new_entry) return;
  prewarmer_->PrewarmFamily(family.GetString());
}

} // namespace bkit

namespace bkit {

// FirstAvailableOrFirst. The source trims only
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
    if (Get().IsFamilyAvailable(family)) return family;
  }
  return families[0];
}

} // namespace bkit
