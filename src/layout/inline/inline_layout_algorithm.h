// Standalone adapter for Blink inline collection, shaping and line placement.
#pragma once

#include <array>
#include <optional>
#include <span>

#include "base/hash_map.h"
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
    // Overflow and multi-pass breaking disable both paragraph optimizers.
    bool disable_score_and_bisect = false;
  };
  // LineBreakPoint of the score line breaker.
  struct BreakPoint {
    unsigned offset;
    bool is_hyphenated;
  };
  // Candidate measurement keeps paragraph ranges; views and paint fragments
  // are materialized only for the chosen line. Reshaped edges retain their view.
  struct LineShape {
    const ShapeResult* result = nullptr;
    std::shared_ptr<const ShapeResultView> view;
    unsigned start = 0;
    unsigned end = 0;
    LineShape() = default;
    explicit LineShape(const ShapeResult* result, unsigned start, unsigned end)
        : result(result), start(start), end(end) {}
    explicit LineShape(std::shared_ptr<const ShapeResultView> view)
        : view(std::move(view)), start(this->view->StartIndex()), end(this->view->EndIndex()) {}
    explicit operator bool() const { return result || view; }
    LayoutUnit SnappedWidth() const;
    LineShape SubRange(unsigned from, unsigned to) const;
    std::shared_ptr<const ShapeResultView> CreateView() const;
  };
  struct LineItem {
    const InlineObject* object_ = nullptr;
    LineShape shape_;
    std::shared_ptr<const TextCombine> text_combine_;
    String generated_text_;
    TextOffsetRange text_offset_;
    LayoutUnit inline_size_;
    FragmentItem::ItemType type_ = FragmentItem::kText;
    unsigned bidi_level_ = 0;
    bool line_break_ = false;
    const ComputedStyle& Style() const { return object_->Style(); }
    bool IsGeneratedText() const { return type_ == FragmentItem::kGeneratedText; }
    bool IsAtomicInline() const { return type_ == FragmentItem::kBox; }
    bool IsLineBreak() const { return line_break_; }
  };
  struct ShapedLine {
    HeapVector<LineItem> items;
    unsigned start = 0;
    unsigned end = 0;
    TextDirection direction = TextDirection::kLtr;
    bool hyphenated = false;
    bool valid = false;
  };
  struct HyphenResult {
    String text;
    std::shared_ptr<const ShapeResultView> shape;
    LayoutUnit InlineSize() const { return shape->SnappedWidth().ClampNegativeToZero(); }
  };
  struct CachedLineBreak {
    unsigned start = 0;
    LayoutUnit available_width;
    TextDirection direction = TextDirection::kLtr;
    LineBreakResult result{0, false};
    HeapVector<LineItem> items;
    bool valid = false;
  };
  LineBreakResult BreakLine(unsigned start, LayoutUnit available_width);
  unsigned ParagraphEnd(unsigned start) const;
  bool UsesBreakSpaces(unsigned start, unsigned end) const;
  bool ComputeScoreBreakPoints(unsigned start, bool is_balanced);
  std::optional<LayoutUnit> AttemptParagraphBalancing();
  void Collect(const InlineObject&, InlineItemsBuilder&);
  LayoutUnit HangingTrailingSpaceWidth(std::span<const LineItem>, LayoutUnit width, bool is_last_line) const;
  void RemoveTrailingCollapsibleSpace(HeapVector<LineItem>&, unsigned start, unsigned end) const;
  void SegmentAndShape(bool use_latin1_script, bool is_bidi_enabled);
  void SplitShapingRuns();
  // Returns whether Blink would allocate InlineItemSegments, including a
  // single mixed-vertical segment. Such text is not eligible for partial reuse.
  bool SegmentText(bool use_latin1_script);
  void ApplyTextAutoSpace();
  void ReuseCollectedItems(FragmentItems&);
  const HyphenResult& HyphenForStyle(const ComputedStyle&);
  HeapVector<LineItem> ShapeLine(unsigned start, unsigned end, bool hyphenated = false);
  ShapedLine& GetShapedLine(unsigned start, unsigned end, bool hyphenated);
  LayoutUnit Measure(unsigned start, unsigned end, bool* has_content = nullptr, bool hyphenated = false);
  LayoutUnit TextIndent(unsigned line_start) const;
  bool ApplyJustification(LayoutUnit space, unsigned end_offset, HeapVector<FragmentItem>&);
  unsigned NextBreakOpportunity(LazyLineBreakIterator&, unsigned offset, unsigned limit,
                                bool break_anywhere = false, bool disable_phrase = false,
                                bool advance_empty = true) const;
  const ComputedStyle& StyleAtOffset(unsigned offset) const;
  void PlaceLine(unsigned start, unsigned end, bool soft_wrap, bool hyphenated = false,
                 bool use_greedy_result = false);
  void FinishLine(HeapVector<LineItem>&, unsigned start, unsigned end, bool hyphenated);
  unsigned ReuseLines();
  void ToPhysicalCoordinates();

  InlineFormattingContext& context_;
  const InlineLayoutOptions& options_;
  std::unique_ptr<FragmentItems> result_;
  OffsetMappingBuilder mapping_builder_;
  HeapVector<Run> runs_;
  // Scratch data for segmentation/autospace; snapshots keep levels on runs.
  Vector<UBiDiLevel> levels_;
  Vector<unsigned> forced_break_offsets_;
  // Includes the final style at index inline_items_.size().
  Vector<const ComputedStyle*> break_styles_before_;
  bool has_preserved_tabs_ = false;
  // ScoreLineBreakContext::SuspendUntilEndParagraph().
  std::optional<unsigned> score_suspended_until_;
  Vector<const InlineObject*> continuing_boxes_;
  wtf_size_t continuing_box_item_index_ = 0;
  // Fixed-break shaping for score breaking and width measurement.
  std::array<ShapedLine, 2> shaped_lines_;
  unsigned next_shaped_line_ = 0;
  // ScoreLineBreaker keeps greedy lookahead results across successive lines. Keep
  // their decisions and selected shapes for the largest window (balance: 6).
  std::array<CachedLineBreak, 6> line_breaks_;
  wtf_size_t next_line_break_ = 0;
  HashMap<const ComputedStyle*, HyphenResult> hyphens_;
  LayoutUnit block_offset_;
  // The base direction of the current line: the block's 'direction', or the
  // paragraph's for 'unicode-bidi: plaintext'.
  TextDirection base_direction_ = TextDirection::kLtr;
  // The available width of the line being broken or placed: the container's,
  // or the balanced width of 'text-wrap-style: balance'.
  LayoutUnit available_width_;
  std::optional<LazyLineBreakIterator> breaks_;
  Vector<BreakPoint> break_points_;
  wtf_size_t break_point_index_ = 0;
};

} // namespace bkit
