// Ported from: blink/renderer/core/layout/inline/fragment_items.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "base/hash_map.h"
#include "layout/text_combine.h"

#include <span>
#include <unicode/ubidi.h>

#include "base/hash_map.h"
#include "base/heap_vector.h"
#include "base/vector.h"
#include "layout/inline/fragment_item.h"
#include "layout/inline/offset_mapping.h"

namespace bkit {

// Subset of Blink's InlineItem: a range of the text content from one object,
// with the collapsing state InlineItemsBuilder needs. Empty and collapsed-away
// text items and inline container boundaries have zero length.
struct InlineItem {
  enum InlineItemType {
    kText,
    // Forced breaks, tabs, ignored controls and generated break opportunities.
    kControl,
    kAtomicInline,
    kOpenTag,
    kCloseTag,
    // Bidi controls injected for unicode-bidi; no fragments.
    kBidiControl
  };
  // Whether the end of this item is collapsible or not, and if so, whether the
  // trailing collapsible space is collapsed (removed) or not.
  enum CollapseType {
    // This item is not collapsible.
    kNotCollapsible,
    // This item is collapsible; i.e., ends with a collapsible space.
    kCollapsible,
    // This item ends with a collapsible space that is collapsed.
    kCollapsed,
    // This item is opaque to whitespace collapsing.
    kOpaqueToCollapsing
  };
  const InlineObject* object;
  unsigned start;
  unsigned end;
  InlineItemType type = kText;
  CollapseType end_collapse_type = kNotCollapsible;
  // True if the collapsible space run at the end contains a newline.
  bool is_end_collapsible_newline = false;
  // Generated break opportunities participate in breaking, but create no
  // fragments and have no source character in the offset mapping.
  bool is_generated_for_line_break = false;
};

// A shaping run of the collected inline items: consecutive items with the same
// font and bidi level.
struct InlineItemRun {
  unsigned start;
  unsigned end;
  UBiDiLevel level;
  std::shared_ptr<const ComputedStyle> style;
  const InlineObject* object;
  std::shared_ptr<ShapeResult> shape;
  bool control;
};

class FragmentItems {
public:
  std::span<const FragmentItem> Items() const {
    return {items_.data(), items_.size()};
  }
  const FragmentItem& operator[](size_t i) const {
    return items_[i];
  }
  size_t Size() const {
    return items_.size();
  }
  const OffsetMapping& Mapping() const {
    return *mapping_;
  }
  const String& TextContent() const {
    return mapping_->GetText();
  }
  PhysicalSize SizeInPhysicalCoordinates() const {
    return physical_size_;
  }
  // The block's writing mode and direction when laid out.
  WritingMode GetWritingMode() const { return writing_mode_; }
  TextDirection Direction() const { return direction_; }
  // One-based, as in Blink. Zero denotes no fragments in this snapshot.
  size_t FirstInlineFragmentItemIndex(const InlineObject&) const;
  std::span<const size_t> Lines() const {
    return {lines_.data(), lines_.size()};
  }
  size_t ReusedLineCount() const {
    return reused_line_count_;
  }

private:
  friend class InlineFormattingContext;
  friend class InlineLayoutAlgorithm;
  void FinalizeAfterLayout();
  void DirtyLinesFromChangedChild(const InlineObject&);
  void DirtyTextRange(const InlineObject&, unsigned offset);
  void DirtyFirstItem();
  void RefreshStyle(const InlineObject&, const std::shared_ptr<const ComputedStyle>&);
  bool TryDirtyFirstLineFor(const InlineObject&);
  bool TryDirtyLastLineFor(const InlineObject&);
  void DirtyLine(size_t item_index);

  HeapVector<FragmentItem> items_;
  Vector<size_t> lines_;
  HashMap<const InlineObject*, size_t> first_items_;
  // Shared with later layouts that reuse the collected items, as
  // InlineNodeData keeps its OffsetMapping.
  std::shared_ptr<const OffsetMapping> mapping_;
  // Script/orientation/fallback segmentation is context dependent, even when
  // the text and bidi level before an edit are unchanged.
  Vector<uint32_t> shaping_context_;
  // InlineNodeData equivalent. The next layout reuses the collected runs,
  // their shape results and the bidi levels unless NeedsCollectInlines().
  HeapVector<InlineItem> inline_items_;
  HeapVector<InlineItemRun> runs_;
  Vector<UBiDiLevel> levels_;
  PhysicalSize physical_size_;
  WritingMode writing_mode_ = WritingMode::kHorizontalTb;
  // LayoutTextCombine boxes by the object whose text they combine.
  HashMap<const InlineObject*, std::shared_ptr<const TextCombine>> text_combines_;
  TextDirection direction_ = TextDirection::kLtr;
  size_t reused_line_count_ = 0;
};

} // namespace bkit
