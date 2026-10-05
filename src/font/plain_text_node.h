// Ported from: blink/renderer/platform/fonts/plain_text_node.h
// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "shaping/frame_shape_cache.h"
#include "base/vector.h"
#include "platform_export.h"
#include "text/text_direction.h"
#include "base/text/wtf_string.h"
#include "paint/geometry.h"

namespace bkfont {

class Font;
class FrameShapeCache;
class ShapeResult;
class ShapeResultView;
class TextRun;
struct CharacterRange;

// PlainTextItem represents a sub-segment of a PlainTextNode.
class PLATFORM_EXPORT PlainTextItem {
public:
  // `start` - The start offset in the owner text content.
  // `length` - The code unit length of this item.
  //  `dir` - Text direction of this item.
  // `text_content` - The text of the owner PlainTextNode.
  PlainTextItem(wtf_size_t start,
                wtf_size_t length,
                TextDirection dir,
                const String& text_content);
  ~PlainTextItem();
  PlainTextItem(PlainTextItem&&) noexcept;
  PlainTextItem& operator=(PlainTextItem&&) noexcept;
  PlainTextItem(const PlainTextItem&) = delete;
  PlainTextItem& operator=(const PlainTextItem&) = delete;

  wtf_size_t StartOffset() const {
    return start_offset_;
  }
  wtf_size_t EndOffset() const {
    return start_offset_ + length_;
  }
  wtf_size_t Length() const {
    return length_;
  }
  TextDirection Direction() const {
    return direction_;
  }
  const ShapeResult* GetShapeResult() const {
    return shape_result_.get();
  }
  const ShapeResultView* EnsureView() const;
  const String& Text() const {
    return text_;
  }
  const RectF& InkBounds() const {
    return ink_bounds_;
  }

private:
  friend class PlainTextNode;
  friend class FrameShapeCacheTest;

  std::shared_ptr<const ShapeResult> shape_result_;
  // Created on demand and owned by this item. EnsureView() borrows the view.
  mutable std::unique_ptr<ShapeResultView> shape_result_view_;
  RectF ink_bounds_;
  String text_;
  wtf_size_t start_offset_;
  wtf_size_t length_;
  TextDirection direction_;
};

// We chose "25" so that it's enough for Chars-chartjs suite in Speedometer3.
using PlainTextItemList = Vector<PlainTextItem, 25>;

// PlainTextNode class represents the information necessary to render a single
// TextRun instance.
//
// This includes a list of substrings after Bidi reordering and word
// segmentation, as well as their ShapeResult.
//
// Instances of this class are immutable.
class PLATFORM_EXPORT PlainTextNode {
public:
  // normalize_space - Enables canvas-specific whitespace normalization
  PlainTextNode(const TextRun& run,
                bool normalize_space,
                const Font& font,
                bool supports_bidi,
                FrameShapeCache* cache);

  PlainTextNode(const PlainTextNode&) = delete;
  PlainTextNode& operator=(const PlainTextNode&) = delete;

  float AccumulateInlineSize(RectF* glyph_bounds) const;
  CharacterRange ComputeCharacterRange(unsigned absolute_from,
                                       unsigned absolute_to) const;

  // The text contains:
  //  - Normalized whitespace
  //  - No BiDi override controls
  const String& TextContent() const {
    return text_content_;
  }
  TextDirection BaseDirection() const {
    return base_direction_;
  }
  bool ContainsRtlItems() const {
    return contains_rtl_items_;
  }
  bool HasVerticalOffsets() const {
    return has_vertical_offsets_;
  }
  const PlainTextItemList& ItemList() const {
    return item_list_;
  }

private:
  friend class PlainTextNodeTest;
  friend class FrameShapeCacheTest;

  // Up-converts to UTF-16 as needed and normalizes spaces and Unicode control
  // characters as per the CSS Text Module Level 3 specification.
  // https://drafts.csswg.org/css-text-3/#white-space-processing
  // Also, check if BiDi reorder is necessary.
  static std::pair<String, bool> NormalizeSpacesAndMaybeBidi(
      StringView text,
      bool normalize_canvas_space);

  void SegmentText(const TextRun& run,
                   bool bidi_overridden,
                   const Font& font,
                   bool supports_bidi);
  void SegmentWord(wtf_size_t start_offset,
                   wtf_size_t run_length,
                   TextDirection direction,
                   const Font& font);

  void Shape(const Font& font, FrameShapeCache* cache);

  String text_content_;
  PlainTextItemList item_list_;
  bool normalize_space_ = false;
  TextDirection base_direction_ = TextDirection::kLtr;
  bool contains_rtl_items_ = false;
  bool has_vertical_offsets_ = false;
};

} // namespace bkfont
