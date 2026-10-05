// Standalone adapter for Blink inline collection, shaping and line placement.
#pragma once

#include "base/heap_vector.h"
#include "base/text/string_builder.h"
#include "base/vector.h"
#include "layout/inline/inline_formatting_context.h"
#include "text/bidi_paragraph.h"

namespace bkfont {

class InlineLayoutAlgorithm {
public:
  explicit InlineLayoutAlgorithm(InlineFormattingContext&);
  std::unique_ptr<FragmentItems> Layout();

private:
  struct Run {
    unsigned start;
    unsigned end;
    UBiDiLevel level;
    std::shared_ptr<const InlineStyle> style;
    const InlineObject* object;
    std::shared_ptr<ShapeResult> shape;
    bool control;
  };
  void Collect(const InlineObject&, std::shared_ptr<const InlineStyle>, StringBuilder&);
  void SegmentAndShape();
  HeapVector<FragmentItem> ShapeLine(unsigned start, unsigned end);
  LayoutUnit Measure(unsigned start, unsigned end);
  void PlaceLine(unsigned start, unsigned end, bool soft_wrap);
  unsigned ReuseLines();
  void ToPhysicalCoordinates();

  InlineFormattingContext& context_;
  const InlineLayoutOptions& options_;
  std::unique_ptr<FragmentItems> result_;
  HeapVector<std::shared_ptr<const InlineStyle>> styles_;
  HeapVector<Run> runs_;
  Vector<UBiDiLevel> levels_;
  LayoutUnit block_offset_;
};

} // namespace bkfont
