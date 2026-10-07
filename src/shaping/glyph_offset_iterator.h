// Ported from: blink/renderer/platform/fonts/shaping/glyph_offset_iterator.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>
#include "base/containers/span.h"
#include "glyph_data.h"
#include "glyph_data_range.h"

namespace bkit {

// An iterator for `ShapeResultRun::offsets_`.
//
// Since it could be empty if there are no glyph offsets in the run, this
// iterator makes iterating offsets to be no-operations in such case.
template <bool has_non_zero_glyph_offsets>
struct GlyphOffsetIterator final {};

// For non-zero glyph offset array
template <>
struct GlyphOffsetIterator<true> final {

public:
  explicit GlyphOffsetIterator(base::span<const GlyphOffset> offsets)
      : iterator_(offsets.begin()) {
    // An empty span should use `has_non_zero_glyph_offsets = false`.
  }

  // The constructor for ShapeResultView
  explicit GlyphOffsetIterator(const GlyphDataRange& range)
      : GlyphOffsetIterator(range.Offsets()) {
  }

  GlyphOffset operator*() const {
    return *iterator_;
  }
  void operator++() {
    ++iterator_;
  }
  void operator+=(ptrdiff_t s) {
    iterator_ += s;
  }

  GlyphOffset operator[](size_t i) const {
    return *(iterator_ + i);
  }

private:
  base::span<const GlyphOffset>::iterator iterator_;
};

// For zero glyph offset array
template <>
struct GlyphOffsetIterator<false> final {
  explicit GlyphOffsetIterator(base::span<const GlyphOffset> offsets) {
    // An empty span should use `has_non_zero_glyph_offsets = false`.
  }

  explicit GlyphOffsetIterator(const GlyphDataRange& range) {
  }

  GlyphOffset operator*() const {
    return GlyphOffset();
  }
  void operator++() {
  }
  void operator+=(ptrdiff_t) {
  }
  GlyphOffset operator[](size_t) const {
    return GlyphOffset();
  }
};

} // namespace bkit
