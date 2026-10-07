// Ported from: blink/renderer/platform/text/hyphenation/hyphenation_minikin.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The dictionary is read from the directory set by
// Hyphenation::SetDictionaryDirectory() instead of the browser's
// mojom::blink::Hyphenation service, with the same file names
// (content/browser/hyphenation/hyphenation_impl.cc: "hyph-<locale>.hyb").
#pragma once

#include <cstdint>
#include <memory>

#include "base/memory/scoped_refptr.h"
#include "base/vector.h"
#include "text/hyphenation.h"

namespace android {
class Hyphenator;
} // namespace android

namespace bkit {

class HyphenationMinikin final : public Hyphenation {
public:
  ~HyphenationMinikin() override;

  bool OpenDictionary(const AtomicString& locale);

  wtf_size_t LastHyphenLocation(const StringView& text, wtf_size_t before_index) const override;
  Vector<wtf_size_t, 8> HyphenLocations(const StringView&) const override;

  // Extract the word to hyphenate by skipping leading and trailing spaces and
  // punctuations.
  static StringView WordToHyphenate(const StringView& text, unsigned* num_leading_chars_out);

  static AtomicString MapLocale(const AtomicString& locale);

  static scoped_refptr<HyphenationMinikin> FromDataForTesting(const AtomicString& locale, Vector<uint8_t> data);

private:
  bool OpenDictionary(Vector<uint8_t> data);

  Vector<uint8_t> Hyphenate(const StringView&) const;

  // The dictionary contents (memory-mapped upstream).
  Vector<uint8_t> file_;
  std::unique_ptr<android::Hyphenator> hyphenator_;
};

} // namespace bkit
