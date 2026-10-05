// Ported from: blink/renderer/core/layout/inline/inline_items_builder.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Subset for the local model: text, forced breaks, generated break
// opportunities and atomic inlines. Inline containers are culled and have no
// bidi controls, so their open/close items (opaque to collapsing) are omitted.
// There is no text-transform, ::first-letter, ruby, SVG, float or
// out-of-flow content. `text-wrap-mode` is still the IFC-wide wrap option.
#pragma once

#include "base/heap_vector.h"
#include "base/text/string_builder.h"
#include "base/text/string_view.h"
#include "layout/inline/fragment_items.h"
#include "layout/inline/offset_mapping_builder.h"

namespace bkfont {

class InlineItemsBuilder {
public:
  InlineItemsBuilder(HeapVector<InlineItem>* items, OffsetMappingBuilder* mapping_builder, bool auto_wrap)
      : items_(items),
        mapping_builder_(mapping_builder),
        auto_wrap_(auto_wrap) {
  }
  InlineItemsBuilder(const InlineItemsBuilder&) = delete;
  InlineItemsBuilder& operator=(const InlineItemsBuilder&) = delete;

  String ToString() {
    return text_.ToString();
  }

  // Append a string from a text object, with whitespace processing per its
  // `white-space-collapse`.
  void AppendText(const InlineObject& layout_text);

  // Append an atomic inline; it is not collapsible.
  void AppendAtomicInline(const InlineObject& layout_object);

  // Segment Break Transformation Rules define to keep trailing new lines, but
  // they are removed in Phase II. Trailing collapsible spaces are not added in
  // Phase I.
  void ExitBlock();

private:
  void AppendTextItem(StringView, const InlineObject&);
  InlineItem& AppendTextItem(InlineItem::InlineItemType, StringView, const InlineObject&);
  void AppendEmptyTextItem(const InlineObject&);
  void AppendGeneratedBreakOpportunity(const InlineObject&);
  void AppendTransformedString(StringView);
  void AppendCollapseWhitespace(StringView, const ComputedStyle&, const InlineObject&);
  void AppendPreserveWhitespace(StringView, const ComputedStyle&, const InlineObject&);
  void AppendPreserveNewline(StringView, const ComputedStyle&, const InlineObject&);
  void AppendForcedBreak(const InlineObject&);
  void AppendForcedBreakCollapseWhitespace(const InlineObject&);
  InlineItem& AppendBreakOpportunity(const InlineObject&);
  InlineItem& Append(InlineItem::InlineItemType, UChar, const InlineObject&);
  InlineItem& AppendOpaque(InlineItem::InlineItemType, UChar, const InlineObject&);
  bool ShouldInsertBreakOpportunityAfterLeadingPreservedSpaces(StringView, const ComputedStyle&,
                                                               unsigned index) const;
  void InsertBreakOpportunityAfterLeadingPreservedSpaces(StringView, const ComputedStyle&, const InlineObject&,
                                                         unsigned* start);
  InlineItem* LastItemToCollapseWith();
  void RemoveTrailingCollapsibleSpaceIfExists();
  void RemoveTrailingCollapsibleSpace(InlineItem*);
  void RestoreTrailingCollapsibleSpaceIfRemoved();
  void RestoreTrailingCollapsibleSpace(InlineItem*);

  HeapVector<InlineItem>* items_;
  OffsetMappingBuilder* mapping_builder_;
  // ShouldWrapLine() of every item: the IFC-wide wrap option.
  const bool auto_wrap_;
  StringBuilder text_;
};

} // namespace bkfont
