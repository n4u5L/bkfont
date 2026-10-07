// Ported from: blink/renderer/platform/fonts/shaping/glyph_data_range.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/containers/span.h"
#include "base/forward.h"
#include "glyph_data.h"

namespace bkit {

struct ShapeResultRun;

// Represents a range of HarfBuzzRunGlyphData. |begin| and |end| follow the
// iterator pattern; i.e., |begin| is lower or equal to |end| in the address
// space regardless of LTR/RTL. |begin| is inclusive, |end| is exclusive.
// This is a borrowed view: the run and its glyph storage must outlive the range
// and must not be resized while the range is used.
class GlyphDataRange {

public:
  GlyphDataRange() = default;
  explicit GlyphDataRange(const ShapeResultRun&);

  unsigned size() const {
    return size_;
  }
  bool IsEmpty() const {
    return !size_;
  }

  // The `span` of `HarfBuzzRunGlyphData`.
  base::span<const HarfBuzzRunGlyphData> Glyphs() const;

  using const_iterator = const HarfBuzzRunGlyphData*;
  const_iterator begin() const;
  const_iterator end() const;

  bool HasOffsets() const;

  // The `span` of `GlyphOffset` if `HasOffsets()`, or an empty span.
  base::span<const GlyphOffset> Offsets() const;

  GlyphDataRange FindGlyphDataRange(bool is_rtl,
                                    unsigned start_character_index,
                                    unsigned end_character_index) const;

private:
  GlyphDataRange(const GlyphDataRange&,
                 const_iterator begin,
                 const_iterator end);

  const ShapeResultRun* run_ = nullptr;
  wtf_size_t index_ = 0;
  wtf_size_t size_ = 0;
};

} // namespace bkit
