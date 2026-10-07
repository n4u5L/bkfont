// Ported from: blink/renderer/platform/fonts/shaping/glyph_index_result.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace bkit {

struct GlyphIndexResult {

public:
  // Those are the left and right character indexes of the group of glyphs
  // that were selected by OffsetForPosition.
  unsigned left_character_index = 0;
  unsigned right_character_index = 0;

  // The glyph origin of the glyph.
  float origin_x = 0;
  // The advance of the glyph.
  float advance = 0;
};

} // namespace bkit
