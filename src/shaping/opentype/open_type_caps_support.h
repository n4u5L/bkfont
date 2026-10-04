// Ported from Chromium: third_party/blink/renderer/platform/fonts/opentype/open_type_caps_support.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include "font/font_description.h"
#include "shaping/case_mapping_harfbuzz_buffer_filler.h"
#include "shaping/harfbuzz_face.h"
#include "shaping/small_caps_iterator.h"

#include <hb.h>

namespace bkfont {

class OpenTypeCapsSupport {

public:
  OpenTypeCapsSupport();
  OpenTypeCapsSupport(
      const HarfBuzzFace*,
      FontDescription::FontVariantCaps requested_caps,
      FontDescription::FontSynthesisSmallCaps font_synthesis_small_caps,
      hb_script_t);

  bool NeedsRunCaseSplitting();
  bool NeedsSyntheticFont(SmallCapsIterator::SmallCapsBehavior run_case);
  FontDescription::FontVariantCaps FontFeatureToUse(
      SmallCapsIterator::SmallCapsBehavior run_case);
  CaseMapIntend NeedsCaseChange(SmallCapsIterator::SmallCapsBehavior run_case);

private:
  enum class FontFormat {
    kUndetermined,
    kOpenType,
    kAat
  };
  // Lazily intializes font_format_ when needed and returns the format of the
  // underlying HarfBuzzFace/Font.
  FontFormat GetFontFormat() const;
  void DetermineFontSupport(hb_script_t);
  bool SupportsFeature(hb_script_t, uint32_t tag) const;
  bool SupportsAatFeature(uint32_t tag) const;
  bool SupportsOpenTypeFeature(hb_script_t, uint32_t tag) const;
  bool SyntheticSmallCapsAllowed() const;

  const HarfBuzzFace* harfbuzz_face_ = nullptr;
  FontDescription::FontVariantCaps requested_caps_ =
      FontDescription::kCapsNormal;
  FontDescription::FontSynthesisSmallCaps font_synthesis_small_caps_ =
      FontDescription::kAutoFontSynthesisSmallCaps;

  enum class FontSupport {
    kFull,
    kFallback, // Fall back to 'smcp' or 'smcp' + 'c2sc'
    kNone
  };

  enum class CapsSynthesis {
    kNone,
    kLowerToSmallCaps,
    kUpperToSmallCaps,
    kBothToSmallCaps
  };

  FontSupport font_support_;
  CapsSynthesis caps_synthesis_;
  mutable FontFormat font_format_;
};

} // namespace bkfont
