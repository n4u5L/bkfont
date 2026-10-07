// Ported from: blink/renderer/platform/text/hyphenation.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <filesystem>

#include <cassert>

#include "base/vector.h"
#include "base/text/string_view.h"
#include "platform_export.h"
#include "base/forward.h"
#include "base/ref_counted.h"
#include "base/text/atomic_string_hash.h"
#include "base/text/string_hash.h"
#include "base/text/unicode.h"
#include "base/wtf_size_t.h"
#include "base/memory/scoped_refptr.h"
#include "base/text/atomic_string.h"

namespace bkit {

// The embedding application supplies the dictionary-backed implementation of
// LastHyphenLocation; the shared limits and search algorithms match Blink.
class PLATFORM_EXPORT Hyphenation : public RefCounted<Hyphenation> {
public:
  virtual ~Hyphenation() = default;

  // Find the last hyphenation location before |before_index|.
  // Returns 0 if no hyphenation locations were found.
  virtual wtf_size_t LastHyphenLocation(const StringView&,
                                        wtf_size_t before_index) const = 0;

  // Find the first hyphenation location after |after_index|.
  // Returns 0 if no hyphenation locations were found.
  virtual wtf_size_t FirstHyphenLocation(const StringView&,
                                         wtf_size_t after_index) const;

  // Find all hyphenation locations in the reverse order.
  virtual Vector<wtf_size_t, 8> HyphenLocations(const StringView&) const;

  // The minimum number of characters in the word / before the hyphen / after
  // the hyphen. `0` means that the UA chooses a value.
  wtf_size_t MinPrefixLength() const {
    return min_prefix_length_;
  }
  wtf_size_t MinSuffixLength() const {
    return min_suffix_length_;
  }
  wtf_size_t MinWordLength() const {
    return min_word_length_;
  }
  void SetLimits(wtf_size_t min_prefix_length,
                 wtf_size_t min_suffix_length,
                 wtf_size_t min_word_length);
  void ResetLimits() {
    SetLimits(0, 0, 0);
  }

  // The hyphenation of a lower-case locale, or null when no dictionary is
  // available (hyphenation_minikin.cc).
  static scoped_refptr<Hyphenation> PlatformGetHyphenation(const AtomicString& locale);
  // Host replacement for the browser's hyphenation dictionary service: the
  // directory with "hyph-<locale>.hyb" files. Empty disables hyphens: auto.
  static void SetDictionaryDirectory(std::filesystem::path);

protected:
  void Initialize(const AtomicString& locale);

  bool ShouldHyphenateWord(const StringView& word) const {
    assert((MinWordLength()) >= (1u));
    if (word.length() < MinWordLength())
      return false;
    // Avoid hyphenating capitalized words.
    return hyphenate_capitalized_word_ || !unicode::IsUpper(word[0]);
  }

private:
  // The default values suggested by the spec:
  // https://drafts.csswg.org/css-text-4/#propdef-hyphenate-limit-chars
  static constexpr unsigned kDefaultMinPrefixLength = 2;
  static constexpr unsigned kDefaultMinSuffixLength = 2;
  static constexpr unsigned kDefaultMinWordLength = 5;
  static_assert(kDefaultMinWordLength >=
                kDefaultMinPrefixLength + kDefaultMinSuffixLength);
  wtf_size_t min_word_length_ = kDefaultMinWordLength;
  wtf_size_t min_prefix_length_ = kDefaultMinPrefixLength;
  wtf_size_t min_suffix_length_ = kDefaultMinSuffixLength;

  bool hyphenate_capitalized_word_ = false;
};

} // namespace bkit
