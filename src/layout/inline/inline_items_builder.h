// Ported from: blink/renderer/core/layout/inline/inline_items_builder.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Subset for the local model: text, whitespace controls, generated break
// opportunities, atomic inlines, bidi controls for unicode-bidi and inline
// container boundaries. Containers do not generate box fragments.
// There is no ::first-letter, ruby, SVG, float or out-of-flow content.
#pragma once

#include "base/heap_vector.h"
#include "base/text/string_builder.h"
#include "base/text/character_names.h"
#include "base/text/string_view.h"
#include "layout/inline/fragment_items.h"
#include "layout/inline/offset_mapping_builder.h"
#include "layout/inline/transformed_string.h"

namespace bkit {

class InlineItemsBuilder {
public:
  // `block_object` is the root object; it owns the bidi controls of the
  // block itself.
  InlineItemsBuilder(HeapVector<InlineItem>* items, OffsetMappingBuilder* mapping_builder,
                     const InlineObject& block_object)
      : items_(items),
        mapping_builder_(mapping_builder),
        block_object_(&block_object) {
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

  // The block container is the root object; its unicode-bidi applies to the
  // paragraph.
  void EnterBlock(const ComputedStyle&);
  void EnterInline(const InlineObject&);
  void ExitInline(const InlineObject&);

  bool HasBidiControls() const { return has_bidi_controls_; }
  bool HasUnicodeBidiPlainText() const { return has_unicode_bidi_plain_text_; }

  // Segment Break Transformation Rules define to keep trailing new lines, but
  // they are removed in Phase II. Trailing collapsible spaces are not added in
  // Phase I.
  void ExitBlock();

private:
  struct BidiContext {
    const InlineObject* node;
    UChar enter;
    UChar exit;
  };
  void EnterBidiContext(const InlineObject*, UChar enter, UChar exit);
  void EnterBidiContext(const InlineObject*, const ComputedStyle&, UChar ltr_enter, UChar rtl_enter, UChar exit);
  void Exit(const InlineObject*);
  void AppendTextItem(const TransformedString&, const InlineObject&);
  InlineItem& AppendTextItem(InlineItem::InlineItemType, const TransformedString&, const InlineObject&);
  void AppendEmptyTextItem(const InlineObject&);
  void AppendGeneratedBreakOpportunity(const InlineObject&);
  void AppendTransformedString(const TransformedString&);
  void AppendCollapseWhitespace(const TransformedString&, const ComputedStyle&, const InlineObject&);
  void AppendPreserveWhitespace(const TransformedString&, const ComputedStyle&, const InlineObject&);
  void AppendPreserveNewline(const TransformedString&, const ComputedStyle&, const InlineObject&);
  void AppendForcedBreak(const InlineObject&);
  void AppendForcedBreakCollapseWhitespace(const InlineObject&);
  InlineItem& AppendBreakOpportunity(const InlineObject&);
  InlineItem& Append(InlineItem::InlineItemType, UChar, const InlineObject&);
  InlineItem& AppendOpaque(InlineItem::InlineItemType, UChar, const InlineObject*);
  bool ShouldInsertBreakOpportunityAfterLeadingPreservedSpaces(StringView, const ComputedStyle&,
                                                               unsigned index) const;
  void InsertBreakOpportunityAfterLeadingPreservedSpaces(const TransformedString&, const ComputedStyle&,
                                                         const InlineObject&, unsigned* start);
  InlineItem* LastItemToCollapseWith();
  void RemoveTrailingCollapsibleSpaceIfExists();
  void RemoveTrailingCollapsibleSpace(InlineItem*);
  void RestoreTrailingCollapsibleSpaceIfRemoved();
  void RestoreTrailingCollapsibleSpace(InlineItem*);

  HeapVector<InlineItem>* items_;
  OffsetMappingBuilder* mapping_builder_;
  const InlineObject* block_object_;
  StringBuilder text_;
  Vector<BidiContext> bidi_context_;
  bool has_bidi_controls_ = false;
  bool has_unicode_bidi_plain_text_ = false;
  // LayoutText::PreviousCharacter() for text-transform: capitalize, the last
  // character of the previous non-empty text in the formatting context.
  UChar previous_character_ = uchar::kSpace;
};

} // namespace bkit
