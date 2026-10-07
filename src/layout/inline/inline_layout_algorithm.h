// Standalone adapter for Blink inline collection, shaping and line placement.
#pragma once

#include <optional>

#include "base/heap_vector.h"
#include "base/text/string_builder.h"
#include "base/vector.h"
#include "layout/inline/inline_formatting_context.h"
#include "layout/inline/inline_items_builder.h"
#include "layout/inline/offset_mapping_builder.h"
#include "text/bidi_paragraph.h"

namespace bkit {

class InlineLayoutAlgorithm {
public:
  explicit InlineLayoutAlgorithm(InlineFormattingContext&);
  std::unique_ptr<FragmentItems> Layout();

private:
  using Run = InlineItemRun;
  struct LineBreakResult {
    unsigned end;
    bool is_hyphenated;
  };
  // LineBreakPoint of the score line breaker.
  struct BreakPoint {
    unsigned offset;
    bool is_hyphenated;
  };
  LineBreakResult BreakLine(unsigned start, LayoutUnit available_width);
  float ContentWidth(unsigned from, unsigned to) const;
  bool ComputeScoreBreakPoints(unsigned start, bool is_balanced);
  std::optional<LayoutUnit> AttemptParagraphBalancing();
  void Collect(const InlineObject&, InlineItemsBuilder&);
  LayoutUnit HangingTrailingSpaceWidth(const HeapVector<FragmentItem>&, LayoutUnit width, bool is_last_line) const;
  void RemoveTrailingCollapsibleSpace(HeapVector<FragmentItem>&, unsigned start, unsigned end) const;
  void SegmentAndShape();
  void ApplyTextAutoSpace();
  void ReuseCollectedItems(const FragmentItems&);
  HeapVector<FragmentItem> ShapeLine(unsigned start, unsigned end, bool hyphenated = false);
  LayoutUnit Measure(unsigned start, unsigned end, bool* has_content = nullptr, bool hyphenated = false);
  LayoutUnit TextIndent(unsigned line_start) const;
  unsigned Hyphenate(unsigned line_start, unsigned word_start, unsigned word_end);
  bool ApplyJustification(LayoutUnit space, unsigned end_offset, HeapVector<FragmentItem>&);
  unsigned NextBreakOpportunity(LazyLineBreakIterator&, unsigned offset, unsigned limit,
                                bool break_anywhere = false) const;
  const ComputedStyle& StyleAtOffset(unsigned offset) const;
  void PlaceLine(unsigned start, unsigned end, bool soft_wrap, bool hyphenated = false);
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
  // The base direction of the current line: the block's 'direction', or the
  // paragraph's for 'unicode-bidi: plaintext'.
  TextDirection base_direction_ = TextDirection::kLtr;
  // The available width of the line being broken or placed: the container's,
  // or the balanced width of 'text-wrap-style: balance'.
  LayoutUnit available_width_;
  std::optional<LazyLineBreakIterator> breaks_;
  std::optional<LazyLineBreakIterator> emergency_breaks_;
  Vector<BreakPoint> break_points_;
  wtf_size_t break_point_index_ = 0;
};

} // namespace bkit
