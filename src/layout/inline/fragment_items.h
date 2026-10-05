// Ported from: blink/renderer/core/layout/inline/fragment_items.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <span>
#include <unordered_map>

#include "base/heap_vector.h"
#include "base/vector.h"
#include "layout/inline/fragment_item.h"
#include "layout/inline/offset_mapping.h"

namespace bkfont {

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
    return mapping_;
  }
  const String& TextContent() const {
    return mapping_.GetText();
  }
  PhysicalSize SizeInPhysicalCoordinates() const {
    return physical_size_;
  }
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
  bool TryDirtyFirstLineFor(const InlineObject&);
  bool TryDirtyLastLineFor(const InlineObject&);
  void DirtyLine(size_t item_index);

  HeapVector<FragmentItem> items_;
  Vector<size_t> lines_;
  std::unordered_map<const InlineObject*, size_t> first_items_;
  OffsetMapping mapping_;
  // Script/orientation/fallback segmentation is context dependent, even when
  // the text and bidi level before an edit are unchanged.
  Vector<uint32_t> shaping_context_;
  PhysicalSize physical_size_;
  size_t reused_line_count_ = 0;
};

} // namespace bkfont
