/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2006, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (c) 2007, 2008, 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

// Font matching/measurement facade from platform/fonts/font.cc.
#include "font.h"
#include <utility>
#include "font_fallback_map.h"
#include "runtime_enabled_features.h"
#include <cmath>
#include "character_range.h"
#include "shaping/caching_word_shaper.h"
#include "text/bidi_paragraph.h"
#include "base/notreached.h"
namespace bkfont {
static std::shared_ptr<FontFallbackList> GetOrCreateFontFallbackList(const FontDescription& description,
                                                                     FontSelector* selector) {
  FontFallbackMap& map = selector ? selector->GetFontFallbackMap() : FontCache::Get().GetFontFallbackMap();
  return map.Get(description);
}
Font::Font() = default;
Font::Font(const FontDescription& description)
    : font_description_(description) {
}
Font::Font(const FontDescription& description, std::shared_ptr<FontSelector> selector)
    : font_description_(description),
      font_fallback_list_(selector ? GetOrCreateFontFallbackList(description, selector.get()) : nullptr) {
}
FontFallbackList* Font::EnsureFontFallbackList() const {
  if (!font_fallback_list_ || !font_fallback_list_->IsValid())
    font_fallback_list_ = GetOrCreateFontFallbackList(font_description_, GetFontSelector());
  return font_fallback_list_.get();
}
// Port source: third_party/blink/renderer/platform/fonts/font.cc
bool Font::operator==(const Font& other) const {
  // Font objects with the same FontDescription and FontSelector should always
  // hold reference to the same FontFallbackList object, unless invalidated.
  if (font_fallback_list_ && font_fallback_list_->IsValid() && other.font_fallback_list_ && other.font_fallback_list_->IsValid()) {
    return font_fallback_list_ == other.font_fallback_list_;
  }

  return GetFontSelector() == other.GetFontSelector() && font_description_ == other.font_description_;
}

std::span<const FontFeatureRange> Font::GetFontFeatures() const {
  return EnsureFontFallbackList()->GetFontFeatures(font_description_);
}

bool Font::HasNonInitialFontFeatures() const {
  return EnsureFontFallbackList()->HasNonInitialFontFeatures(font_description_);
}

ShapeCache* Font::GetShapeCache() const {
  return EnsureFontFallbackList()->GetShapeCache(font_description_);
}

bool Font::CanShapeWordByWord() const {
  return EnsureFontFallbackList()->CanShapeWordByWord(GetFontDescription());
}

void Font::ReportNotDefGlyph() const {
  FontSelector* fontSelector = EnsureFontFallbackList()->GetFontSelector();
  // We have a few non-DOM usages of Font code, for example in DragImage::Create
  // and in EmbeddedObjectPainter::paintReplaced. In those cases, we can't
  // retrieve a font selector as our connection to a Document object to report
  // UseCounter metrics, and thus we cannot report notdef glyphs.
  if (fontSelector)
    fontSelector->ReportNotDefGlyph();
}

void Font::ReportEmojiSegmentGlyphCoverage(unsigned num_clusters,
                                           unsigned num_broken_clusters) const {
  FontSelector* fontSelector = EnsureFontFallbackList()->GetFontSelector();
  // See ReportNotDefGlyph(), sometimes no fontSelector is available in non-DOM
  // usages of Font.
  if (fontSelector) {
    fontSelector->ReportEmojiSegmentGlyphCoverage(num_clusters,
                                                  num_broken_clusters);
  }
}

void Font::WillUseFontData(const String& text) const {
  const FontDescription& font_description = GetFontDescription();
  const FontFamily& family = font_description.Family();
  if (family.FamilyName().empty()) [[unlikely]] {
    return;
  }
  if (FontSelector* font_selector = GetFontSelector()) {
    font_selector->WillUseFontData(font_description, family, text);
    return;
  }
  // Non-DOM usages can't resolve generic family.
  if (family.IsPrewarmed() || family.FamilyIsGeneric())
    return;
  family.SetIsPrewarmed();
  FontCache::PrewarmFamily(family.FamilyName());
}

float Font::TextAutoSpaceInlineSize() const {
  if (const SimpleFontData* font_data = PrimaryFont()) {
    return font_data->TextAutoSpaceInlineSize();
  }

  NOTREACHED();
}

std::pair<float, bool> Font::TabWidthInternal(const SimpleFontData* font_data,
                                              const TabSize& tab_size) const {
  const auto& font_description = GetFontDescription();
  float letter_spacing = font_description.LetterSpacing();
  if (!font_data) {
    return {letter_spacing, false};
  }
  float word_spacing = font_description.WordSpacing();
  float base_tab_width = tab_size.GetPixelSize(font_data->SpaceWidth(),
                                               letter_spacing,
                                               word_spacing);
  if (!base_tab_width) {
    return {letter_spacing, false};
  }
  return {base_tab_width, true};
}

float Font::TabWidth(const SimpleFontData* font_data,
                     const TabSize& tab_size,
                     float position) const {
  float base_tab_width = TabWidth(font_data, tab_size);
  if (!base_tab_width)
    return GetFontDescription().LetterSpacing();

  float modulized_position = fmodf(position, base_tab_width);
  if (RuntimeEnabledFeatures::TabWidthNegativePositionEnabled() && modulized_position < 0) [[unlikely]] {
    modulized_position += base_tab_width;
  }

  float distance_to_tab_stop = base_tab_width - modulized_position;

  // Let the minimum width be the half of the space width so that it's always
  // recognizable.  if the distance to the next tab stop is less than that,
  // advance an additional tab stop.
  if (distance_to_tab_stop < font_data->SpaceWidth() / 2)
    distance_to_tab_stop += base_tab_width;

  return distance_to_tab_stop;
}

LayoutUnit Font::TabWidth(const TabSize& tab_size, LayoutUnit position) const {
  const SimpleFontData* font_data = PrimaryFont();
  auto [base_tab_width, is_successed] = TabWidthInternal(font_data, tab_size);
  if (!is_successed) {
    return LayoutUnit::FromFloatCeil(base_tab_width);
  }

  float modulized_position = fmodf(position, base_tab_width);
  if (RuntimeEnabledFeatures::TabWidthNegativePositionEnabled() && modulized_position < 0) [[unlikely]] {
    modulized_position += base_tab_width;
  }

  LayoutUnit distance_to_tab_stop =
      LayoutUnit::FromFloatFloor(base_tab_width - modulized_position);

  // Let the minimum width be the half of the space width so that it's always
  // recognizable.  if the distance to the next tab stop is less than that,
  // advance an additional tab stop.
  if (distance_to_tab_stop < font_data->SpaceWidth() / 2)
    distance_to_tab_stop += base_tab_width;

  return distance_to_tab_stop;
}

bool Font::IsFallbackValid() const {
  return !font_fallback_list_ || font_fallback_list_->IsValid();
}

} // namespace bkfont

namespace bkfont {
float Font::DeprecatedWidth(const TextRun& run,
                            gfx::RectF* glyph_bounds) const {
  CachingWordShaper shaper(*this);
  return shaper.Width(run, glyph_bounds);
}

float Font::DeprecatedSubRunWidth(const TextRun& run,
                                  unsigned from,
                                  unsigned to,
                                  gfx::RectF* glyph_bounds) const {
  if (run.length() == 0) {
    return 0;
  }

  CachingWordShaper shaper(*this);

  // Run bidi algorithm on the given text. Step 5 of:
  // https://html.spec.whatwg.org/multipage/canvas.html#text-preparation-algorithm
  String text16 = run.ToStringView().ToString();
  text16.Ensure16Bit();
  BidiParagraph bidi;
  bidi.SetParagraph(text16, run.Direction());
  BidiParagraph::Runs runs;
  bidi.GetVisualRuns(text16, &runs);

  float x_pos = 0;
  for (const BidiParagraph::Run& visual_run : runs) {
    if (visual_run.end <= from || to <= visual_run.start) {
      continue;
    }
    // Calculate the required indexes for this specific run.
    unsigned run_from = from < visual_run.start ? 0 : from - visual_run.start;
    unsigned run_to =
        to > visual_run.end ? visual_run.Length() : to - visual_run.start;

    // Measure the subrun.
    TextRun text_run(
        StringView(run.ToStringView(), visual_run.start, visual_run.Length()),
        visual_run.Direction(),
        /* directional_override */ false,
        /* normalize_space */ true);
    CharacterRange character_range =
        shaper.GetCharacterRange(text_run, run_from, run_to);

    // Accumulate the position and the glyph bounding box.
    if (glyph_bounds) {
      gfx::RectF range_bounds(character_range.start, -character_range.ascent, character_range.Width(), character_range.Height());
      // GetCharacterRange() returns bounds positioned as if the whole run was
      // there, so the rect has to be moved to align with the current position.
      range_bounds.Offset(-range_bounds.x() + x_pos, 0);
      glyph_bounds->Union(range_bounds);
    }
    x_pos += character_range.Width();
  }
  if (glyph_bounds != nullptr) {
    glyph_bounds->Offset(-glyph_bounds->x(), 0);
  }
  return x_pos;
}

} // namespace bkfont

namespace bkfont {
GlyphData Font::GetEmphasisMarkGlyphData(const AtomicString& mark) const {
  if (mark.empty())
    return GlyphData();
  return CachingWordShaper(*this).EmphasisMarkGlyphData(TextRun(mark));
}

int Font::EmphasisMarkAscent(const AtomicString& mark) const {

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data.get();
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Ascent();
}

int Font::EmphasisMarkDescent(const AtomicString& mark) const {

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data.get();
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Descent();
}

int Font::EmphasisMarkHeight(const AtomicString& mark) const {

  const auto mark_glyph_data = GetEmphasisMarkGlyphData(mark);
  const SimpleFontData* mark_font_data = mark_glyph_data.font_data.get();
  if (!mark_font_data)
    return 0;

  return mark_font_data->GetFontMetrics().Height();
}

} // namespace bkfont
