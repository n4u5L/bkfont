// Port source: third_party/blink/renderer/platform/fonts/font_fallback_iterator.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "font_data_for_range_set.h"
#include "font_fallback_priority.h"
#include "wtf/hash_set.h"
#include "wtf/text/wtf_uchar.h"

#include <memory>
#include <span>
#include "wtf/vector.h"
#include "wtf/hash_map.h"
namespace blink {

class FontDescription;
class FontFallbackList;
class SimpleFontData;

class FontFallbackIterator {

public:
  using HintCharList = Vector<UChar32, 16>;

  FontFallbackIterator(const FontDescription&,
                       FontFallbackList*,
                       FontFallbackPriority);
  FontFallbackIterator(FontFallbackIterator&&) = default;
  FontFallbackIterator(const FontFallbackIterator&) = delete;
  FontFallbackIterator& operator=(const FontFallbackIterator&) = delete;

  bool operator==(const FontFallbackIterator& other) const;
  bool operator!=(const FontFallbackIterator& other) const {
    return !(*this == other);
  }

  bool HasNext() const {
    return fallback_stage_ != kOutOfLuck;
  }
  // Returns whether the next call to Next() needs a full hint list, or whether
  // a single character is sufficient. Intended to serve as an optimization in
  // HarfBuzzShaper to avoid spending too much time and resources collecting a
  // full hint character list. Returns true when the next font in line is a
  // segmented font, i.e. one that requires the hint list to work out which
  // unicode range segment should be used.
  bool NeedsHintList() const;

  // Some system fallback APIs (Windows, Android) require a character, or a
  // portion of the string to be passed.  On Mac and Linux, we get a list of
  // fonts without passing in characters.
  std::shared_ptr<FontDataForRangeSet> Next(const HintCharList& hint_list);

  void Reset();

private:
  bool RangeSetContributesForHint(const HintCharList& hint_list,
                                  const std::shared_ptr<FontDataForRangeSet>&);
  bool AlreadyLoadingRangeForHintChar(UChar32 hint_char);
  void WillUseRange(const AtomicString& family, const FontDataForRangeSet&);

  std::shared_ptr<FontDataForRangeSet> UniqueOrNext(std::shared_ptr<FontDataForRangeSet> candidate,
                                                    const HintCharList& hint_list);

  std::shared_ptr<const SimpleFontData> FallbackPriorityFont(UChar32 hint);
  std::shared_ptr<const SimpleFontData> UniqueSystemFontForHintList(
      const HintCharList& hint_list);

  const FontDescription& font_description_;
  FontFallbackList* font_fallback_list_;
  int current_font_data_index_;
  unsigned segmented_face_index_;

  enum FallbackStage {
    kFallbackPriorityFonts,
    kFontGroupFonts,
    kSegmentedFace,
    kPreferencesFonts,
    kSystemFonts,
    kFirstCandidateForNotdefGlyph,
    kOutOfLuck
  };

  FallbackStage fallback_stage_;
  HashSet<UChar32> previously_asked_for_hint_;
  // FontFallbackIterator is meant for single use by HarfBuzzShaper,
  // traversing through the fonts for shaping only once. We must not return
  // duplicate FontDataForRangeSet objects from the Next() iteration function
  // as returning a duplicate value causes a shaping run that won't return any
  // results. The exception is that if all fonts fail, we return the first
  // candidate to be used for rendering the .notdef glyph, and set HasNext() to
  // false.
  HashSet<uint64_t> unique_font_data_for_range_sets_returned_;
  std::shared_ptr<FontDataForRangeSet> first_candidate_ = nullptr;
  Vector<std::shared_ptr<FontDataForRangeSet>> tracked_loading_range_sets_;
  FontFallbackPriority font_fallback_priority_;
};

} // namespace blink
