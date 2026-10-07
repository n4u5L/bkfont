// Ported from: blink/renderer/platform/text/hyphenation/hyphenation_minikin.cc
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "text/hyphenation/hyphenation_minikin.h"

#include <unicode/uchar.h>

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>

#include "base/text/utf16.h"
#include "text/character.h"
#include "text/hyphenation/hyphenator_aosp.h"
#include "text/layout_locale.h"

namespace bkit {

namespace {

inline bool ShouldSkipLeadingChar(UChar32 c) {
  if (Character::TreatAsSpace(c)) return true;
  // Strip leading punctuation, defined as OP and QU line breaking classes,
  // see UAX #14.
  const int32_t lb = u_getIntPropertyValue(c, UCHAR_LINE_BREAK);
  if (lb == U_LB_OPEN_PUNCTUATION || lb == U_LB_QUOTATION) return true;
  return false;
}

inline bool ShouldSkipTrailingChar(UChar32 c) {
  // Strip trailing spaces, punctuation and control characters.
  const int32_t gc_mask = U_GET_GC_MASK(c);
  return gc_mask & (U_GC_ZS_MASK | U_GC_P_MASK | U_GC_CC_MASK);
}

std::mutex& DictionaryDirectoryLock() {
  static std::mutex lock;
  return lock;
}

std::filesystem::path& DictionaryDirectory() {
  static std::filesystem::path directory;
  return directory;
}

// HyphenationImpl::OpenDictionary(): the file of a lower-case locale.
Vector<uint8_t> ReadDictionary(const AtomicString& locale) {
  std::filesystem::path directory;
  {
    std::lock_guard<std::mutex> guard(DictionaryDirectoryLock());
    directory = DictionaryDirectory();
  }
  Vector<uint8_t> data;
  if (directory.empty()) return data;
  const std::string filename = "hyph-" + locale.Ascii() + ".hyb";
  std::ifstream file(directory / filename, std::ios::binary);
  if (!file) return data;
  const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  data.reserve(static_cast<wtf_size_t>(contents.size()));
  for (char c : contents) data.push_back(static_cast<uint8_t>(c));
  return data;
}

} // namespace

using Hyphenator = android::Hyphenator;

void Hyphenation::SetDictionaryDirectory(std::filesystem::path directory) {
  std::lock_guard<std::mutex> guard(DictionaryDirectoryLock());
  DictionaryDirectory() = std::move(directory);
}

HyphenationMinikin::~HyphenationMinikin() = default;

bool HyphenationMinikin::OpenDictionary(const AtomicString& locale) {
  return OpenDictionary(ReadDictionary(locale));
}

bool HyphenationMinikin::OpenDictionary(Vector<uint8_t> data) {
  if (data.empty()) return false;
  file_ = std::move(data);
  hyphenator_.reset(Hyphenator::loadBinary(file_.data()));
  return true;
}

StringView HyphenationMinikin::WordToHyphenate(const StringView& text, unsigned* num_leading_chars_out) {
  if (text.Is8Bit()) {
    wtf_size_t begin = 0u;
    wtf_size_t end = text.length();
    while (begin != end && ShouldSkipLeadingChar(text[begin])) ++begin;
    while (begin != end && ShouldSkipTrailingChar(text[end - 1])) --end;
    *num_leading_chars_out = begin;
    assert(end >= begin);
    return StringView(text, begin, end - begin);
  }
  base::span<const UChar> span = text.Span16();
  wtf_size_t index = 0;
  wtf_size_t len = text.length();
  while (index < len) {
    wtf_size_t next_index = index;
    UChar32 c = CodePointAtAndNext(span, next_index);
    if (!ShouldSkipLeadingChar(c)) break;
    index = next_index;
  }
  while (index < len) {
    wtf_size_t prev_len = len;
    UChar32 c = CodePointAtAndPrevious(span, index, prev_len);
    if (!ShouldSkipTrailingChar(c)) break;
    len = prev_len;
  }
  *num_leading_chars_out = index;
  assert(len >= index);
  return StringView(text, index, len - index);
}

Vector<uint8_t> HyphenationMinikin::Hyphenate(const StringView& text) const {
  assert(ShouldHyphenateWord(text));
  assert(text.length() >= MinWordLength());
  Vector<uint8_t> result;
  String text16_bit = text.ToString();
  text16_bit.Ensure16Bit();
  hyphenator_->hyphenate(&result, reinterpret_cast<const uint16_t*>(text16_bit.Span16().data()),
                         text16_bit.length());
  return result;
}

wtf_size_t HyphenationMinikin::LastHyphenLocation(const StringView& text, wtf_size_t before_index) const {
  unsigned num_leading_chars;
  const StringView word = WordToHyphenate(text, &num_leading_chars);
  if (before_index <= num_leading_chars || !ShouldHyphenateWord(word)) return 0;
  assert(word.length() >= MinWordLength());

  assert(word.length() > MinSuffixLength());
  before_index = std::min<wtf_size_t>(before_index - num_leading_chars, word.length() - MinSuffixLength() + 1);
  const wtf_size_t min_prefix_len = MinPrefixLength();
  if (before_index <= min_prefix_len) return 0;

  Vector<uint8_t> result = Hyphenate(word);
  assert(before_index <= result.size());
  assert(before_index >= 1u);
  for (wtf_size_t i = before_index - 1; i >= min_prefix_len; i--) {
    if (result[i]) return i + num_leading_chars;
  }
  return 0;
}

Vector<wtf_size_t, 8> HyphenationMinikin::HyphenLocations(const StringView& text) const {
  unsigned num_leading_chars;
  StringView word = WordToHyphenate(text, &num_leading_chars);

  Vector<wtf_size_t, 8> hyphen_locations;
  if (!ShouldHyphenateWord(word)) return hyphen_locations;
  assert(word.length() >= MinWordLength());

  Vector<uint8_t> result = Hyphenate(word);
  const wtf_size_t min_prefix_len = MinPrefixLength();
  assert(word.length() > MinSuffixLength());
  for (wtf_size_t i = word.length() - MinSuffixLength(); i >= min_prefix_len; --i) {
    if (result[i]) hyphen_locations.push_back(i + num_leading_chars);
  }
  return hyphen_locations;
}

// static
AtomicString HyphenationMinikin::MapLocale(const AtomicString& locale) {
  // This data is from CLDR, compiled by AOSP.
  // https://android.googlesource.com/platform/frameworks/base/+/master/core/jni/android_text_Hyphenator.cpp
  struct LocaleFallback {
    const char* locale;
    const char* data_locale;
    const char* locale_for_exact_match;
  };
  static const LocaleFallback locale_fallback_data[] = {
      // English locales that fall back to en-US. The data is from CLDR. It's
      // all English locales, minus the locales whose parent is en-001 (from
      // supplementalData.xml, under <parentLocales>).
      {"en-AS", "en-us", nullptr}, // English (American Samoa)
      {"en-GU", "en-us", nullptr}, // English (Guam)
      {"en-MH", "en-us", nullptr}, // English (Marshall Islands)
      {"en-MP", "en-us", nullptr}, // English (Northern Mariana Islands)
      {"en-PR", "en-us", nullptr}, // English (Puerto Rico)
      {"en-UM", "en-us", nullptr}, // English (United States Minor Outlying Islands)
      {"en-VI", "en-us", nullptr}, // English (Virgin Islands)
      // All English locales other than those falling back to en-US are mapped
      // to en-GB, except that "en" is mapped to "en-us" for interoperability
      // with other browsers.
      {"en", "en-gb", "en-us"},
      // For German, we're assuming the 1996 (and later) orthography by default.
      {"de", "de-1996", nullptr},
      // Liechtenstein uses the Swiss hyphenation rules for the 1901
      // orthography.
      {"de-LI-1901", "de-ch-1901", nullptr},
      // Norwegian is very probably Norwegian Bokmål.
      {"no", "nb", nullptr},
      // Use mn-Cyrl. According to CLDR's likelySubtags.xml, mn is most likely
      // to be mn-Cyrl.
      {"mn", "mn-cyrl", nullptr}, // Mongolian
      // Fall back to Ethiopic script for languages likely to be written in
      // Ethiopic. Data is from CLDR's likelySubtags.xml.
      {"am", "und-ethi", nullptr},  // Amharic
      {"byn", "und-ethi", nullptr}, // Blin
      {"gez", "und-ethi", nullptr}, // Geʻez
      {"ti", "und-ethi", nullptr},  // Tigrinya
      {"wal", "und-ethi", nullptr}, // Wolaytta
      // Use Hindi as a fallback hyphenator for all languages written in
      // Devanagari, etc. This makes sense because our Indic patterns are not
      // really linguistic, but script-based.
      {"und-Beng", "bn", nullptr}, // Bengali
      {"und-Deva", "hi", nullptr}, // Devanagari -> Hindi
      {"und-Gujr", "gu", nullptr}, // Gujarati
      {"und-Guru", "pa", nullptr}, // Gurmukhi -> Punjabi
      {"und-Knda", "kn", nullptr}, // Kannada
      {"und-Mlym", "ml", nullptr}, // Malayalam
      {"und-Orya", "or", nullptr}, // Oriya
      {"und-Taml", "ta", nullptr}, // Tamil
      {"und-Telu", "te", nullptr}, // Telugu

      // List of locales with hyphens not to fall back.
      {"de-1901", "de-1901", nullptr},
      {"de-1996", "de-1996", nullptr},
      {"de-ch-1901", "de-ch-1901", nullptr},
      {"en-gb", "en-gb", nullptr},
      {"en-us", "en-us", nullptr},
      {"mn-cyrl", "mn-cyrl", nullptr},
      {"und-ethi", "und-ethi", nullptr},
  };
  // The upstream map uses case-folding keys.
  const auto find = [](const AtomicString& key) -> const LocaleFallback* {
    const AtomicString lower = key.LowerASCII();
    for (const auto& entry : locale_fallback_data)
      if (AtomicString(entry.locale).LowerASCII() == lower) return &entry;
    return nullptr;
  };
  for (AtomicString mapped_locale = locale;;) {
    if (const LocaleFallback* it = find(mapped_locale)) {
      if (it->locale_for_exact_match && locale == mapped_locale) return AtomicString(it->locale_for_exact_match);
      return AtomicString(it->data_locale);
    }
    const wtf_size_t last_hyphen = mapped_locale.ReverseFind('-');
    if (last_hyphen == kNotFound || !last_hyphen) return mapped_locale;
    mapped_locale = AtomicString(mapped_locale.GetString().Left(last_hyphen));
  }
}

scoped_refptr<Hyphenation> Hyphenation::PlatformGetHyphenation(const AtomicString& locale) {
  const AtomicString mapped_locale = HyphenationMinikin::MapLocale(locale);
  if (mapped_locale.LowerASCII() != locale.LowerASCII()) {
    Hyphenation* hyphenation = LayoutLocale::Get(mapped_locale)->GetHyphenation();
    return scoped_refptr<Hyphenation>(hyphenation);
  }

  scoped_refptr<HyphenationMinikin> hyphenation(base::AdoptRef(new HyphenationMinikin));
  const AtomicString lower_ascii_locale = locale.LowerASCII();
  if (!hyphenation->OpenDictionary(lower_ascii_locale)) return nullptr;
  hyphenation->Initialize(lower_ascii_locale);
  return hyphenation;
}

scoped_refptr<HyphenationMinikin> HyphenationMinikin::FromDataForTesting(const AtomicString& locale,
                                                                         Vector<uint8_t> data) {
  scoped_refptr<HyphenationMinikin> hyphenation(base::AdoptRef(new HyphenationMinikin));
  if (!hyphenation->OpenDictionary(std::move(data))) return nullptr;
  hyphenation->Initialize(locale);
  return hyphenation;
}

} // namespace bkit
