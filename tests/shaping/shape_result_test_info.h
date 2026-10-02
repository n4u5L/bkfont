// Official Chromium test: third_party/blink/renderer/platform/fonts/shaping/shape_result_test_info.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "shaping/harfbuzz_shaper.h"
// Bloberizer test support is excluded: painting/Skia blobs are not ported.
#include "wtf/allocator/allocator.h"

#include "hb.h"

namespace blink {

class ShapeResultTestInfo : public ShapeResult {
public:
  void CheckConsistency() const;
  unsigned NumberOfRunsForTesting() const;
  ShapeResultRun& RunInfoForTesting(unsigned run_index) const;
  bool RunInfoForTesting(unsigned run_index,
                         unsigned& start_index,
                         unsigned& num_glyphs,
                         hb_script_t&) const;
  bool RunInfoForTesting(unsigned run_index,
                         unsigned& start_index,
                         unsigned& num_characters,
                         unsigned& num_glyphs,
                         hb_script_t&) const;
  uint16_t GlyphForTesting(unsigned run_index, unsigned glyph_index) const;
  float AdvanceForTesting(unsigned run_index, unsigned glyph_index) const;
  const SimpleFontData* FontDataForTesting(unsigned run_index) const;
  Vector<unsigned> CharacterIndexesForTesting() const;
};

struct ShapeResultTestGlyphInfo {
  unsigned character_index;
  Glyph glyph;
  float advance;
};

void AddGlyphInfo(void* context,
                  unsigned character_index,
                  Glyph,
                  gfx::Vector2dF glyph_offset,
                  float advance,
                  bool is_horizontal,
                  CanvasRotationInVertical,
                  const SimpleFontData*);

void ComputeGlyphResults(const ShapeResult&,
                         Vector<ShapeResultTestGlyphInfo>*);

bool PLATFORM_EXPORT
CompareResultGlyphs(const Vector<ShapeResultTestGlyphInfo>& test,
                    const Vector<ShapeResultTestGlyphInfo>& reference,
                    unsigned reference_start,
                    unsigned num_glyphs);

} // namespace blink
