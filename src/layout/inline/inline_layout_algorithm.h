// Standalone adapter for Blink inline collection, shaping and line placement.
#pragma once

#include "base/heap_vector.h"
#include "base/text/string_builder.h"
#include "base/vector.h"
#include "layout/inline/inline_formatting_context.h"
#include "layout/inline/inline_items_builder.h"
#include "layout/inline/offset_mapping_builder.h"
#include "text/bidi_paragraph.h"

namespace bkfont {

class InlineLayoutAlgorithm {
public:
  explicit InlineLayoutAlgorithm(InlineFormattingContext&);
  std::unique_ptr<FragmentItems> Layout();

private:
  using Run = InlineItemRun;
  void Collect(const InlineObject&, InlineItemsBuilder&);
  LayoutUnit HangingTrailingSpaceWidth(const HeapVector<FragmentItem>&) const;
  void RemoveTrailingCollapsibleSpace(HeapVector<FragmentItem>&) const;
  void SegmentAndShape();
  void ReuseCollectedItems(const FragmentItems&);
  HeapVector<FragmentItem> ShapeLine(unsigned start, unsigned end);
  LayoutUnit Measure(unsigned start, unsigned end);
  void PlaceLine(unsigned start, unsigned end, bool soft_wrap);
  unsigned ReuseLines();
  void ToPhysicalCoordinates();

  InlineFormattingContext& context_;
  const InlineLayoutOptions& options_;
  std::unique_ptr<FragmentItems> result_;
  OffsetMappingBuilder mapping_builder_;
  HeapVector<std::shared_ptr<const ComputedStyle>> styles_;
  HeapVector<Run> runs_;
  Vector<UBiDiLevel> levels_;
  LayoutUnit block_offset_;
};

} // namespace bkfont
