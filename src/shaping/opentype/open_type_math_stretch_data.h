// Ported from Chromium: third_party/blink/renderer/platform/fonts/opentype/open_type_math_stretch_data.h
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include "font/glyph.h"

#include "base/vector.h"

namespace bkfont {

class OpenTypeMathStretchData {
public:
  enum StretchAxis : uint8_t {
    Horizontal = 0,
    Vertical = 1
  };

  // https://docs.microsoft.com/en-us/typography/opentype/spec/math#mathGlyphVariantRecordFormat
  // Note: Only variantGlyph is considered as using advanceMeasurement can lead
  // to inconsistent values compared to what SimpleFontData returns.
  using GlyphVariantRecord = Glyph;

  // https://docs.microsoft.com/en-us/typography/opentype/spec/math#glyphPartRecord
  struct GlyphPartRecord {
    Glyph glyph;
    float start_connector_length;
    float end_connector_length;
    float full_advance;
    bool is_extender;
  };

  // https://w3c.github.io/mathml-core/#the-glyphassembly-table
  struct AssemblyParameters {
    float connector_overlap{0};
    unsigned repetition_count{0};
    unsigned glyph_count{0};
    float stretch_size{0};
    Vector<GlyphPartRecord> parts;
  };
};

} // namespace bkfont
