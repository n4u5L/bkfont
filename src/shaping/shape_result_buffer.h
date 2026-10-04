// Ported from Chromium: third_party/blink/renderer/platform/fonts/shaping/shape_result_buffer.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <optional>
#include "shape_result.h"

#include "base/vector.h"

namespace bkfont {

struct CharacterRange;
class FontDescription;
struct GlyphData;
class ShapeResultBloberizer;

class ShapeResultBuffer {

public:
  ShapeResultBuffer()
      : has_vertical_offsets_(false) {
  }
  ShapeResultBuffer(const ShapeResultBuffer&) = delete;
  ShapeResultBuffer& operator=(const ShapeResultBuffer&) = delete;

  void AppendResult(const ShapeResult* result) {
    has_vertical_offsets_ |= result->HasVerticalOffsets();
    results_.push_back(result->shared_from_this());
  }

  bool HasVerticalOffsets() const {
    return has_vertical_offsets_;
  }

  CharacterRange GetCharacterRange(const StringView& text,
                                   TextDirection,
                                   float total_width,
                                   unsigned from,
                                   unsigned to) const;

  GlyphData EmphasisMarkGlyphData(const FontDescription&) const;

  struct CharacterRangeContext {
    const StringView& text;
    const bool is_rtl;
    int from;
    int to;
    float current_x;
    unsigned total_num_characters = 0;
    std::optional<float> from_x;
    std::optional<float> to_x;
    float min_y = 0;
    float max_y = 0;
  };
  // A helper for GetCharacterRange().
  static void ComputeRangeIn(const ShapeResult& result,
                             const gfx::RectF& ink_bounds,
                             CharacterRangeContext& context);

private:
  friend class ShapeResultBloberizer;

  // Empirically, cases where we get more than 50 ShapeResults are extremely
  // rare.
  Vector<std::shared_ptr<const ShapeResult>, 64> results_;
  bool has_vertical_offsets_;
};

} // namespace bkfont
