// Ported from: blink/renderer/platform/fonts/shaping/caching_word_shape_iterator.cc
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "caching_word_shape_iterator.h"

#include "harfbuzz_shaper.h"
#include "shape_result.h"
#include "paint/geometry.h"

namespace bkit {

std::shared_ptr<const ShapeResult> CachingWordShapeIterator::ShapeWordWithoutSpacing(
    const TextRun& word_run,
    const Font* font) {
  ShapeCacheEntry* cache_entry = shape_cache_->Add(word_run);
  if (cache_entry && *cache_entry)
    return *cache_entry;

  HarfBuzzShaper shaper(word_run.NormalizedUTF16());
  std::shared_ptr<ShapeResult> shape_result = shaper.Shape(font, word_run.Direction());
  if (!shape_result)
    return nullptr;

  shape_result->SetDeprecatedInkBounds(shape_result->ComputeInkBounds());
  if (cache_entry)
    *cache_entry = shape_result;

  return shape_result;
}

std::shared_ptr<const ShapeResult> CachingWordShapeIterator::ShapeWord(const TextRun& word_run,
                                                                       const Font* font) {
  std::shared_ptr<const ShapeResult> result = ShapeWordWithoutSpacing(word_run, font);
  if (!spacing_.HasSpacing()) [[likely]] {
    return result;
  }

  std::shared_ptr<ShapeResult> spacing_result = result->ApplySpacingToCopy(spacing_, word_run);
  RectF ink_bounds = spacing_result->ComputeInkBounds();

  // Return bounds as is because glyph bounding box is in logical space.
  if (spacing_result->Width() >= 0) {
    spacing_result->SetDeprecatedInkBounds(ink_bounds);
    return spacing_result;
  }

  // Negative word-spacing and/or letter-spacing may cause some glyphs to
  // overflow the left boundary and result negative measured width. Adjust glyph
  // bounds accordingly to cover the overflow.
  // The negative width should be clamped to 0 in CSS box model, but it's up to
  // caller's responsibility.
  float left = std::min(spacing_result->Width(), ink_bounds.width());
  if (left < ink_bounds.x()) {
    // The right edge should be the width of the first character in most cases,
    // but computing it requires re-measuring bounding box of each glyph. Leave
    // it unchanged, which gives an excessive right edge but assures it covers
    // all glyphs.
    ink_bounds.Outset(OutsetsF().set_left(ink_bounds.x() - left));
  }

  spacing_result->SetDeprecatedInkBounds(ink_bounds);
  return spacing_result;
}

} // namespace bkit
