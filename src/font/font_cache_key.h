// Port source: third_party/blink/renderer/platform/fonts/font_cache_key.h
/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Google Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
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

#pragma once

#include <limits>

#include "value_equivalent.h"
#include "wtf/math_extras.h"
#include "target_platform.h"
#include "font_face_creation_params.h"
#include "font_palette.h"
#include "font_size_adjust.h"
#include "font_variant_alternates.h"
#include "font_settings.h"
#include "wtf/hash_table_deleted_value_type.h"
#include "wtf/text/atomic_string_hash.h"
#include "wtf/text/string_hash.h"

#include <span>
#include <string>
namespace blink {

// Multiplying the floating point size by 100 gives two decimal point
// precision which should be sufficient.
static constexpr unsigned kFontSizePrecisionMultiplier = 100;

struct FontCacheKey {

public:
  FontCacheKey() = default;
  FontCacheKey(
      FontFaceCreationParams creation_params,
      float font_size,
      unsigned options,
      float device_scale_factor,
      FontSizeAdjust size_adjust,
      std::shared_ptr<const FontVariationSettings> variation_settings,
      std::shared_ptr<const FontPalette> palette,
      std::shared_ptr<const FontVariantAlternates> font_variant_alternates,
      bool is_unique_match)
      : creation_params_(creation_params),
        font_size_(ClampTo<unsigned>(
            font_size * kFontSizePrecisionMultiplier)),
        options_(options),
        device_scale_factor_(device_scale_factor),
        size_adjust_(size_adjust),
        variation_settings_(std::move(variation_settings)),
        palette_(palette),
        font_variant_alternates_(font_variant_alternates),
        is_unique_match_(is_unique_match) {
  }

  FontCacheKey(HashTableDeletedValueType)
      : font_size_(std::numeric_limits<unsigned>::max()),
        device_scale_factor_(std::numeric_limits<float>::max()) {
  }

  bool IsHashTableDeletedValue() const {
    return font_size_ == std::numeric_limits<unsigned>::max() && device_scale_factor_ == std::numeric_limits<float>::max();
  }

  unsigned GetHash() const {
    // Convert from float with 3 digit precision before hashing.
    unsigned device_scale_factor_hash = device_scale_factor_ * 1000;
    unsigned hash_codes[10] = {
        creation_params_.GetHash(),
        font_size_,
        options_,
        device_scale_factor_hash,
        size_adjust_ ? size_adjust_.GetHash() : 0,
#if BUILDFLAG(IS_ANDROID)
        (locale_.empty() ? 0 : blink::GetHash(locale_)) ^
#endif // BUILDFLAG(IS_ANDROID)
            (variation_settings_ ? variation_settings_->GetHash() : 0),
        palette_ ? palette_->GetHash() : 0,
        font_variant_alternates_ ? font_variant_alternates_->GetHash() : 0,
        is_unique_match_};
    return StringHasher::HashMemory(base::as_byte_span(hash_codes));
  }

  bool operator==(const FontCacheKey& other) const {
    bool variation_settings_equal =
        (!variation_settings_ && !other.variation_settings_) || (variation_settings_ && other.variation_settings_ && *variation_settings_ == *other.variation_settings_);
    bool palette_equal =
        (!palette_ && !other.palette_) || (palette_ && other.palette_ && *palette_ == *other.palette_);
    return creation_params_ == other.creation_params_ && font_size_ == other.font_size_ && options_ == other.options_ && device_scale_factor_ == other.device_scale_factor_ && size_adjust_ == other.size_adjust_ &&
#if BUILDFLAG(IS_ANDROID)
           locale_ == other.locale_ &&
#endif // BUILDFLAG(IS_ANDROID)
           variation_settings_equal && palette_equal && FontValuesEquivalent(font_variant_alternates_, other.font_variant_alternates_) && is_unique_match_ == other.is_unique_match_;
  }

  bool operator!=(const FontCacheKey& other) const {
    return !(*this == other);
  }

  static constexpr unsigned PrecisionMultiplier() {
    return kFontSizePrecisionMultiplier;
  }

#if BUILDFLAG(IS_ANDROID)
  // Set the locale if the font is locale-specific. This allows different
  // |FontPlatformData| instances for each locale.
  void SetLocale(const AtomicString& locale) {
    locale_ = locale.LowerASCII();
  }
#endif // BUILDFLAG(IS_ANDROID)

private:
  FontFaceCreationParams creation_params_;
  unsigned font_size_ = 0;
  unsigned options_ = 0;
  // FontCacheKey is the key to retrieve FontPlatformData entries from the
  // FontCache. FontPlatformData queries the platform's font render style, which
  // is dependent on the device scale factor. That's why we need
  // device_scale_factor_ to be a part of computing the cache key.
  float device_scale_factor_ = 0;
#if BUILDFLAG(IS_ANDROID)
  AtomicString locale_;
#endif // BUILDFLAG(IS_ANDROID)
  FontSizeAdjust size_adjust_;
  std::shared_ptr<const FontVariationSettings> variation_settings_;
  std::shared_ptr<const FontPalette> palette_;
  std::shared_ptr<const FontVariantAlternates> font_variant_alternates_;
  bool is_unique_match_ = false;
};

template <>
struct HashTraits<FontCacheKey> : SimpleClassHashTraits<blink::FontCacheKey> {
  // std::string (inside FontFaceCreationParams) and the std::shared_ptr
  // fields require construction; their empty state need not be all zero.
  static const bool kEmptyValueIsZero = false;
};

// `FontCacheKey` contains an `std::string` (via `FontFaceCreationParams`)
// which contains poisoned metadata for detecting buffer overflows in short
// strings. Copying this string as part of `KeyValuePairExtractor` will thus
// trigger ASAN warnings.

} // namespace blink

template <>
struct std::hash<blink::FontCacheKey> {
  std::size_t operator()(blink::FontCacheKey const& s) const noexcept {
    return static_cast<size_t>(s.GetHash());
  }
};
