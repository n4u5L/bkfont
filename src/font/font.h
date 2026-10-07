// Ported from: blink/renderer/platform/fonts/font.h
// The painting APIs draw through PaintCanvas (see CanvasPaintCanvas).

/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2006, 2007, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Holger Hans Peter Freyther
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

#pragma once
#include <tuple>
#include "base/vector.h"
#include "font_description.h"
#include "paint/paint_canvas.h"
#include "font_fallback_iterator.h"
#include "font_fallback_list.h"
#include "simple_font_data.h"
#include "layout/layout_unit.h"
#include "text/tab_size.h"
namespace bkit {

class TextRun;
struct TextFragmentPaintInfo;
struct TextRunPaintInfo;
class Font {
public:
  Font();
  explicit Font(const FontDescription&);
  Font(const FontDescription&, std::shared_ptr<FontSelector>);
  bool operator==(const Font&) const;
  bool operator!=(const Font& a) const {
    return !(*this == a);
  }
  const FontDescription& GetFontDescription() const {
    return font_description_;
  }

  enum class DrawType { kGlyphsOnly, kGlyphsAndClusters };

  enum CustomFontNotReadyAction {
    kDoNotPaintIfFontNotReady,
    kUseFallbackIfFontNotReady
  };

  void DrawText(PaintCanvas*,
                const TextFragmentPaintInfo&,
                const PointF&,
                NodeId node_id,
                const PlatformPaint&,
                DrawType = DrawType::kGlyphsOnly) const;
  // Deprecated: Use PlainTextPainter.
  bool DeprecatedDrawBidiText(PaintCanvas*,
                              const TextRunPaintInfo&,
                              const PointF&,
                              CustomFontNotReadyAction,
                              const PlatformPaint&,
                              DrawType = DrawType::kGlyphsOnly) const;
  void DrawEmphasisMarks(PaintCanvas*,
                         const TextFragmentPaintInfo&,
                         const AtomicString& mark,
                         const PointF&,
                         const PlatformPaint&) const;

  RectF TextInkBounds(const TextFragmentPaintInfo&) const;

  struct TextIntercept {
    float begin_, end_;
  };

  // Compute the text intercepts along the axis of the advance and write them
  // into the specified Vector of TextIntercepts. The number of those is zero
  // or a multiple of two, and is at most the number of glyphs * 2 in the text
  // part of TextFragmentPaintInfo. Specify bounds for the upper and lower
  // extend of a line crossing through the text, parallel to the baseline.
  // TODO(drott): crbug.com/655154 Fix this for upright in vertical.
  void GetTextIntercepts(const TextFragmentPaintInfo&,
                         const PlatformPaint&,
                         const std::tuple<float, float>& bounds,
                         Vector<TextIntercept>&) const;
  const SimpleFontData* PrimaryFont() const {
    return EnsureFontFallbackList()->PrimarySimpleFontDataWithSpace(font_description_).get();
  }
  const SimpleFontData* PrimaryFontWithDigitZero() const {
    return EnsureFontFallbackList()->PrimarySimpleFontDataWithDigitZero(font_description_).get();
  }
  const SimpleFontData* PrimaryFontWithCjkWater() const {
    return EnsureFontFallbackList()->PrimarySimpleFontDataWithCjkWater(font_description_).get();
  }
  std::span<const FontFeatureRange> GetFontFeatures() const;
  bool HasNonInitialFontFeatures() const;
  bool CanShapeWordByWord() const;
  ShapeCache* GetShapeCache() const;
  FontSelector* GetFontSelector() const {
    return font_fallback_list_ ? font_fallback_list_->GetFontSelector() : nullptr;
  }
  FontFallbackList* EnsureFontFallbackList() const;
  FontFallbackIterator CreateFontFallbackIterator(FontFallbackPriority priority) const {
    return FontFallbackIterator(font_description_, EnsureFontFallbackList(), priority);
  }
  float DeprecatedWidth(const TextRun&, RectF* glyph_bounds = nullptr) const;
  float DeprecatedSubRunWidth(const TextRun&, unsigned from, unsigned to, RectF* glyph_bounds = nullptr) const;
  float SpaceWidth() const {
    return (PrimaryFont() ? PrimaryFont()->SpaceWidth() : 0) + font_description_.LetterSpacing();
  }
  float TabWidth(const SimpleFontData* data, const TabSize& size) const {
    return TabWidthInternal(data, size).first;
  }
  float TabWidth(const SimpleFontData*, const TabSize&, float position) const;
  float TabWidth(const TabSize& size, float position) const {
    return TabWidth(PrimaryFont(), size, position);
  }
  LayoutUnit TabWidth(const TabSize&, LayoutUnit position) const;
  int EmphasisMarkAscent(const AtomicString&) const;
  int EmphasisMarkDescent(const AtomicString&) const;
  int EmphasisMarkHeight(const AtomicString&) const;
  float TextAutoSpaceInlineSize() const;
  void WillUseFontData(const String&) const;
  void ReportNotDefGlyph() const;
  void ReportEmojiSegmentGlyphCoverage(unsigned clusters, unsigned broken) const;
  bool IsFallbackValid() const;
  bool ShouldSkipDrawing() const {
    return font_fallback_list_ && EnsureFontFallbackList()->ShouldSkipDrawing();
  }
  bool HasCustomFont() const {
    return font_fallback_list_ && EnsureFontFallbackList()->HasCustomFont();
  }

private:
  GlyphData GetEmphasisMarkGlyphData(const AtomicString&) const;
  std::pair<float, bool> TabWidthInternal(const SimpleFontData*, const TabSize&) const;
  FontDescription font_description_;
  mutable std::shared_ptr<FontFallbackList> font_fallback_list_;
};

} // namespace bkit
