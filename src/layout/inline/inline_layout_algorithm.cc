// Adapts Blink InlineNode, ShapingLineBreaker and LogicalLineBuilder to a
// direct model. No block formatting, DOM or fragmentainers.
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_layout_algorithm.h"

#include <algorithm>
#include <cassert>
#include <unicode/uchar.h>

#include "base/text/character_names.h"
#include "layout/inline_text_metrics.h"
#include "shaping/harfbuzz_shaper.h"
#include "shaping/shape_result_spacing.h"
#include "shaping/shaping_line_breaker.h"
#include "text/character.h"
#include "text/character_break_iterator.h"

namespace bkfont {
namespace {

bool IsForcedBreak(UChar c) {
  return c == '\n' || c == '\r' || c == 0x2028;
}
bool IsControl(UChar c) {
  return IsForcedBreak(c) || c == '\t';
}
bool IsBidiTrailingSpace(UChar c) {
  return LazyLineBreakIterator::IsBreakableSpace(c) || u_charDirection(c) == U_WHITE_SPACE_NEUTRAL;
}

// LineBreaker::ShapeLineAt's edge reshaping uses the same segmented input and
// spacing as the initial shape. Keeping this adapter here avoids a second
// shaper in the editing/painting paths.
class LineShaper final : public ShapingLineBreaker {
public:
  LineShaper(const String& text, const Font& font, const ShapeResult& shape,
             const LazyLineBreakIterator& breaks)
      : ShapingLineBreaker(&shape, &breaks, nullptr, &font),
        text_(text),
        font_(font),
        direction_(shape.Direction()) {
  }

private:
  std::shared_ptr<const ShapeResult> Shape(unsigned start, unsigned end, ShapeOptions options) override {
    HarfBuzzShaper shaper(text_);
    RunSegmenter segmenter(text_.Span16(), font_.GetFontDescription().Orientation());
    Vector<RunSegmenter::RunSegmenterRange> ranges;
    RunSegmenter::RunSegmenterRange range;
    while (segmenter.Consume(&range)) {
      if (start < range.end && end > range.start) ranges.push_back(range);
      if (range.end >= end) break;
    }
    auto shape = shaper.Shape(&font_, direction_, start, end, ranges, options);
    ShapeResultSpacing<String> spacing(text_);
    if (spacing.SetSpacing(font_.GetFontDescription())) shape->ApplySpacing(spacing);
    return shape;
  }
  const String& text_;
  const Font& font_;
  TextDirection direction_;
};

} // namespace

InlineLayoutAlgorithm::InlineLayoutAlgorithm(InlineFormattingContext& context)
    : context_(context),
      options_(context.Options()),
      result_(std::make_unique<FragmentItems>()) {
}

// InlineNode::CollectInlinesInternal(): inline containers are culled, so only
// leaves append items.
void InlineLayoutAlgorithm::Collect(const InlineObject& object, InlineItemsBuilder& builder) {
  if (object.IsInline()) {
    for (const auto& child : object.Children()) Collect(*child, builder);
    return;
  }
  if (object.IsText())
    builder.AppendText(object);
  else
    builder.AppendAtomicInline(object);
}

void InlineLayoutAlgorithm::SegmentAndShape() {
  const String& text = result_->TextContent();
  levels_ = Vector<UBiDiLevel>(text.length(), IsLtr(options_.direction) ? 0 : 1);
  result_->shaping_context_.resize(text.length());
  if (text.empty()) return;
  BidiParagraph bidi;
  if (bidi.SetParagraph(text, options_.direction)) {
    for (unsigned start = 0; start < text.length();) {
      UBiDiLevel level;
      const unsigned end = bidi.GetLogicalRun(start, &level);
      std::fill(levels_.begin() + start, levels_.begin() + end, level);
      start = end;
    }
  }
  const auto& units = result_->inline_items_;
  for (size_t i = 0; i < units.size(); ++i) {
    const auto& unit = units[i];
    for (unsigned start = unit.start; start < unit.end;) {
      unsigned end = start + 1;
      const bool control =
          unit.type != InlineItem::kText || unit.object->IsAtomicInline() || IsControl(text[start]);
      if (!control) {
        while (end < unit.end && levels_[end] == levels_[start] && !IsControl(text[end])) ++end;
      }
      // InlineNode shapes through model boundaries with the same font. Color
      // and node identity split fragments later, without changing ligatures.
      if (!control && !runs_.empty() && !runs_.back().control && runs_.back().end == start &&
          runs_.back().level == levels_[start] && *runs_.back().style->GetFont() == *styles_[i]->GetFont()) {
        runs_.back().end = end;
      } else {
        runs_.push_back(Run{start, end, levels_[start], styles_[i], unit.object, nullptr, control});
      }
      start = end;
    }
  }
  HarfBuzzShaper shaper(text);
  for (Run& run : runs_) {
    if (run.control) continue;
    RunSegmenter segmenter(text.Span16(), run.style->GetFont()->GetFontDescription().Orientation());
    Vector<RunSegmenter::RunSegmenterRange> ranges;
    RunSegmenter::RunSegmenterRange segment;
    while (segmenter.Consume(&segment)) {
      if (run.start < segment.end && run.end > segment.start) {
        ranges.push_back(segment);
        const uint32_t key = static_cast<uint32_t>(segment.script) |
                             (static_cast<uint32_t>(segment.render_orientation) << 16) |
                             (static_cast<uint32_t>(segment.font_fallback_priority) << 24);
        std::fill(result_->shaping_context_.begin() + std::max(run.start, segment.start),
                  result_->shaping_context_.begin() + std::min(run.end, segment.end), key);
      }
      if (segment.end >= run.end) break;
    }
    run.shape = shaper.Shape(run.style->GetFont(), DirectionFromLevel(run.level), run.start, run.end, ranges);
    ShapeResultSpacing<String> spacing(text);
    if (spacing.SetSpacing(run.style->GetFont()->GetFontDescription())) run.shape->ApplySpacing(spacing);
  }
}

// InlineNode::PrepareLayoutIfNeeded() keeps InlineNodeData when only layout
// inputs changed: text, offset mapping, bidi levels and shape results stay.
// Styles are refreshed; any change that needs reshaping (including font,
// spacing and text edits) sets NeedsCollectInlines instead.
void InlineLayoutAlgorithm::ReuseCollectedItems(const FragmentItems& old) {
  result_->mapping_ = old.mapping_;
  result_->inline_items_ = old.inline_items_;
  result_->shaping_context_ = old.shaping_context_;
  levels_ = old.levels_;
  runs_ = old.runs_;
  for (const auto& item : result_->inline_items_) styles_.push_back(item.object->LayoutStyle());
  for (Run& run : runs_) run.style = run.object->LayoutStyle();
}

HeapVector<FragmentItem> InlineLayoutAlgorithm::ShapeLine(unsigned start, unsigned end) {
  const String& text = result_->TextContent();
  HeapVector<FragmentItem> items;
  LayoutUnit advance;
  unsigned trailing_space = end;
  while (trailing_space > start && (IsBidiTrailingSpace(text[trailing_space - 1]) || IsForcedBreak(text[trailing_space - 1]))) --trailing_space;
  for (const Run& run : runs_) {
    if (run.end <= start) continue;
    if (run.start >= end) break;
    const unsigned run_start = std::max(start, run.start);
    const unsigned run_end = std::min(end, run.end);
    std::shared_ptr<const ShapeResultView> shape;
    if (run.shape) {
      LazyLineBreakIterator breaks(text, run.style->GetFont()->GetFontDescription().Locale(), options_.word_break);
      LineShaper shaper(text, *run.style->GetFont(), *run.shape, breaks);
      shaper.SetLineStart(start);
      shaper.SetIsAfterForcedBreak(start && IsForcedBreak(text[start - 1]));
      shaper.SetTextSpacingTrim(run.style->GetFont()->GetFontDescription().GetTextSpacingTrim());
      shape = shaper.ShapeLineAt(run_start, run_end);
    } else if (text[run_start] == '\t') {
      const auto tab_shape = ShapeResult::CreateForTabulationCharacters(run.style->GetFont(),
                                                                        DirectionFromLevel(run.level), run.style->GetTabSize(), advance.ToFloat(), run_start, 1);
      shape = ShapeResultView::Create(tab_shape.get());
    }
    const auto& units = result_->inline_items_;
    for (size_t i = 0; i < units.size(); ++i) {
      const auto& unit = units[i];
      if (unit.end <= run_start) continue;
      if (unit.start >= run_end) break;
      const unsigned unit_end = std::min(run_end, unit.end);
      const bool split_trailing = run.level != (IsLtr(options_.direction) ? 0 : 1) &&
                                  trailing_space > std::max(run_start, unit.start) && trailing_space < unit_end;
      LayoutUnit unsplit_width;
      if (split_trailing && shape) unsplit_width = ShapeResultView::Create(shape.get(), std::max(run_start, unit.start), unit_end)->SnappedWidth();
      for (unsigned fragment_start = std::max(run_start, unit.start); fragment_start < unit_end;) {
        const unsigned fragment_end = split_trailing && fragment_start < trailing_space ? trailing_space : unit_end;
        FragmentItem item;
        item.object_ = unit.object;
        item.style_ = styles_[i];
        item.type_ = unit.object->IsAtomicInline() ? FragmentItem::kBox : FragmentItem::kText;
        item.text_offset_ = {fragment_start, fragment_end};
        item.bidi_level_ = run.level;
        item.line_break_ = run.control && !unit.object->IsAtomicInline() && IsForcedBreak(text[run_start]);
        if (shape) {
          item.shape_ = ShapeResultView::Create(shape.get(), item.text_offset_.start, item.text_offset_.end);
          item.inline_size_ = item.shape_->SnappedWidth().ClampNegativeToZero();
          if (split_trailing && fragment_start == trailing_space)
            item.inline_size_ = (unsplit_width - items.back().inline_size_).ClampNegativeToZero();
        } else if (unit.object->IsAtomicInline()) {
          item.inline_size_ = unit.object->LayoutAtomicSize().inline_size;
        }
        advance += item.inline_size_;
        items.push_back(std::move(item));
        fragment_start = fragment_end;
      }
    }
  }
  RemoveTrailingCollapsibleSpace(items);
  // HyphenResult::Shape and LogicalLineBuilder::PlaceHyphen. A soft hyphen
  // generates a visible hyphen only when it actually ends a wrapped line.
  if (!items.empty() && end > start && end < text.length() && text[end - 1] == 0x00ad && !IsForcedBreak(text[end])) {
    FragmentItem hyphen;
    const auto& previous = items.back();
    hyphen.object_ = previous.object_;
    hyphen.style_ = previous.style_;
    hyphen.bidi_level_ = previous.bidi_level_;
    hyphen.type_ = FragmentItem::kGeneratedText;
    hyphen.text_offset_ = {end, end};
    const auto* font = hyphen.Style().GetFont()->PrimaryFont();
    hyphen.generated_text_ = font && font->GlyphForCharacter(0x2010) ? String(u"\u2010") : String("-");
    const auto shape = HarfBuzzShaper(hyphen.generated_text_).Shape(hyphen.Style().GetFont(), hyphen.ResolvedDirection());
    hyphen.shape_ = ShapeResultView::Create(shape.get());
    hyphen.inline_size_ = hyphen.shape_->SnappedWidth().ClampNegativeToZero();
    items.push_back(std::move(hyphen));
  }
  return items;
}

// LineBreaker::RemoveTrailingCollapsibleSpace(): the last text item of a
// line drops its trailing collapsible space (one space after Phase I). An item
// that becomes empty is not placed.
// https://drafts.csswg.org/css-text-3/#white-space-phase-2
void InlineLayoutAlgorithm::RemoveTrailingCollapsibleSpace(HeapVector<FragmentItem>& items) const {
  const String& text = result_->TextContent();
  for (wtf_size_t i = items.size(); i;) {
    FragmentItem& item = items[--i];
    if (item.IsAtomicInline() || item.IsLineBreak()) return;
    const auto range = item.text_offset_;
    if (range.start == range.end) continue;
    if (text[range.end - 1] != uchar::kSpace || !item.Style().ShouldCollapseWhiteSpaces()) return;
    if (range.end - range.start == 1) {
      items.EraseAt(i);
      return;
    }
    item.text_offset_.end = range.end - 1;
    if (item.shape_) {
      item.shape_ = ShapeResultView::Create(item.shape_.get(), range.start, range.end - 1);
      item.inline_size_ = item.shape_->SnappedWidth().ClampNegativeToZero();
    }
    return;
  }
}

// Preserved trailing spaces of 'white-space-collapse: preserve' hang
// (LineBreaker::HandleTrailingSpaces, has_only_pre_wrap_trailing_spaces):
// they do not count when fitting or aligning the line. 'break-spaces' spaces
// do not hang, and nowrap lines never break.
LayoutUnit InlineLayoutAlgorithm::HangingTrailingSpaceWidth(const HeapVector<FragmentItem>& items) const {
  if (!options_.wrap) return LayoutUnit();
  const String& text = result_->TextContent();
  LayoutUnit hang;
  for (wtf_size_t i = items.size(); i;) {
    const FragmentItem& item = items[--i];
    if (item.IsGeneratedText() || item.IsAtomicInline()) break;
    if (item.IsLineBreak()) continue;
    const auto& style = item.Style();
    if (!style.ShouldPreserveWhiteSpaces() || style.ShouldBreakSpaces()) break;
    const auto range = item.text_offset_;
    unsigned end = range.end;
    while (end > range.start &&
           (text[end - 1] == uchar::kSpace || Character::IsOtherSpaceSeparator(text[end - 1])))
      --end;
    if (end == range.end) break;
    if (end == range.start || !item.shape_) {
      hang += item.inline_size_;
      if (end == range.start) continue;
      break;
    }
    hang += item.inline_size_ - ShapeResultView::Create(item.shape_.get(), range.start, end)->SnappedWidth();
    break;
  }
  return hang;
}

LayoutUnit InlineLayoutAlgorithm::Measure(unsigned start, unsigned end) {
  const auto items = ShapeLine(start, end);
  LayoutUnit width;
  for (const auto& item : items) width += item.inline_size_;
  return width - HangingTrailingSpaceWidth(items);
}

void InlineLayoutAlgorithm::PlaceLine(unsigned start, unsigned end, bool soft_wrap) {
  HeapVector<FragmentItem> items = ShapeLine(start, end);
  const auto& root_style = context_.root_->LayoutStyle();
  const FontBaseline baseline_type = GetFontBaseline(root_style->GetFont()->GetFontDescription());
  FontHeight metrics = ComputeTextMetrics(*root_style->GetFont(), root_style->LineHeight(), baseline_type).text_metrics;
  LayoutUnit width;
  for (FragmentItem& item : items) {
    const auto& style = item.Style();
    FontHeight item_metrics;
    if (item.IsAtomicInline()) {
      item_metrics = FontHeight(item.object_->LayoutAtomicBaseline(),
                                item.object_->LayoutAtomicSize().block_size - item.object_->LayoutAtomicBaseline());
      item.block_size_ = item.object_->LayoutAtomicSize().block_size;
      item.block_offset_ = -item_metrics.ascent;
    } else {
      const auto text_metrics = ComputeTextMetrics(*style.GetFont(), style.LineHeight(), baseline_type);
      item_metrics = text_metrics.text_metrics;
      item.block_offset_ = text_metrics.text_top;
      item.block_size_ = text_metrics.text_height;
      // InlineBoxState::AccumulateUsedFonts for line-height: normal.
      if (style.LineHeight().IsAuto() && item.shape_) {
        for (const auto& font : item.shape_->UsedFonts()) {
          FontHeight used = font->GetFontMetrics().GetFontHeight(baseline_type);
          used.AddLeading(CalculateLeadingSpace(font->GetFontMetrics().FixedLineSpacing(), used));
          item_metrics.Unite(used);
        }
      }
    }
    item.block_offset_ -= style.LegacyBaselineShift();
    item_metrics.ascent += style.LegacyBaselineShift();
    item_metrics.descent -= style.LegacyBaselineShift();
    metrics.Unite(item_metrics);
    width += item.inline_size_;
  }
  // LogicalLineBuilder::BidiReorder: use resolved levels for runs and the base
  // direction for trailing whitespace items. There are no opaque block items.
  Vector<UBiDiLevel, 32> levels;
  levels.ReserveInitialCapacity(items.size());
  for (const auto& item : items) levels.push_back(static_cast<UBiDiLevel>(item.bidi_level_));
  const String& text = result_->TextContent();
  for (size_t i = items.size(); i > 0; --i) {
    if (items[i - 1].IsGeneratedText()) break;
    const auto range = items[i - 1].text_offset_;
    bool whitespace = true;
    for (unsigned j = range.start; j < range.end; ++j)
      if (!IsBidiTrailingSpace(text[j]) && !IsForcedBreak(text[j])) {
        whitespace = false;
        break;
      }
    if (!whitespace) break;
    levels[i - 1] = IsLtr(options_.direction) ? 0 : 1;
  }
  Vector<int32_t, 32> visual(items.size());
  if (!items.empty()) BidiParagraph::IndicesInVisualOrder(levels, visual);
  FragmentItem line;
  line.type_ = FragmentItem::kLine;
  line.style_ = root_style;
  line.text_offset_ = {start, end};
  line.bidi_level_ = IsLtr(options_.direction) ? 0 : 1;
  line.block_offset_ = block_offset_;
  line.block_size_ = metrics.LineHeight();
  line.baseline_ = metrics.ascent;
  line.inline_size_ = width;
  line.descendants_count_ = items.size() + 1;
  line.soft_wrap_ = soft_wrap;
  // Start alignment, as LineOffsetForTextAlign() does for direction: rtl.
  // Hanging spaces are not considered for alignment.
  LayoutUnit inline_offset = IsLtr(options_.direction)
                                 ? LayoutUnit()
                                 : options_.available_inline_size - (width - HangingTrailingSpaceWidth(items));
  line.inline_offset_ = inline_offset;
  const size_t line_index = result_->items_.size();
  result_->items_.push_back(std::move(line));
  for (int32_t index : visual) {
    FragmentItem& item = items[index];
    item.inline_offset_ = inline_offset;
    item.block_offset_ += block_offset_ + metrics.ascent;
    item.line_index_ = line_index;
    inline_offset += item.inline_size_;
    result_->items_.push_back(std::move(item));
  }
  block_offset_ += metrics.LineHeight();
}

unsigned InlineLayoutAlgorithm::ReuseLines() {
  const FragmentItems* old = context_.fragments_.get();
  if (!old) return 0;
  unsigned end = 0;
  for (size_t index : old->Lines()) {
    const FragmentItem& line = (*old)[index];
    if (line.dirty_ || line.text_offset_.end >= result_->TextContent().length()) break;
    const unsigned line_end = line.text_offset_.end;
    if (StringView(old->TextContent(), end, line_end - end) !=
        StringView(result_->TextContent(), end, line_end - end)) break;
    bool reusable = true;
    for (size_t i = index + 1; i < index + line.descendants_count_; ++i) {
      const auto& item = (*old)[i];
      if (!item.object_->IsAttached()) {
        reusable = false;
        break;
      }
      const auto units = result_->Mapping().GetMappingUnitsForNode(*item.object_);
      const auto old_units = old->Mapping().GetMappingUnitsForNode(*item.object_);
      if (units.empty() || old_units.empty() ||
          units.front().TextContentStart() != old_units.front().TextContentStart() ||
          item.text_offset_.end > units.back().TextContentEnd() || item.object_->LayoutStyle() != item.style_) {
        reusable = false;
        break;
      }
      for (unsigned p = item.text_offset_.start; p < item.text_offset_.end; ++p) {
        if (levels_[p] != item.bidi_level_ || old->shaping_context_[p] != result_->shaping_context_[p]) {
          reusable = false;
          break;
        }
      }
      if (!reusable) break;
    }
    if (!reusable) break;
    result_->items_.AppendRange(old->items_.begin() + index,
                               old->items_.begin() + index + line.descendants_count_);
    block_offset_ = line.block_offset_ + line.block_size_;
    end = line_end;
    ++result_->reused_line_count_;
  }
  return end;
}

void InlineLayoutAlgorithm::ToPhysicalCoordinates() {
  const bool horizontal = IsHorizontalWritingMode(options_.writing_mode);
  result_->physical_size_ = horizontal ? PhysicalSize(options_.available_inline_size, block_offset_) : PhysicalSize(block_offset_, options_.available_inline_size);
  const FragmentItem* line = nullptr;
  for (auto& item : result_->items_) {
    if (item.Type() == FragmentItem::kLine) line = &item;
    if (horizontal) {
      item.rect_ = {item.inline_offset_, item.block_offset_, item.inline_size_, item.block_size_};
    } else {
      LayoutUnit left = IsFlippedBlocksWritingMode(options_.writing_mode) ? block_offset_ - item.block_offset_ - item.block_size_ : item.block_offset_;
      // FragmentItemsBuilder uses ToLineWritingMode(): vertical-lr lines
      // progress rightwards, but their contents still have line-over on right.
      if (IsFlippedLinesWritingMode(options_.writing_mode) && item.Type() != FragmentItem::kLine)
        left = line->block_offset_ + line->block_size_ -
               (item.block_offset_ - line->block_offset_) - item.block_size_;
      const LayoutUnit top = options_.writing_mode == WritingMode::kSidewaysLr ? options_.available_inline_size - item.inline_offset_ - item.inline_size_ : item.inline_offset_;
      item.rect_ = {left, top, item.block_size_, item.inline_size_};
    }
  }
}

std::unique_ptr<FragmentItems> InlineLayoutAlgorithm::Layout() {
  if (context_.needs_collect_inlines_ || !context_.fragments_) {
    InlineItemsBuilder builder(&result_->inline_items_, &mapping_builder_, options_.wrap);
    Collect(*context_.root_, builder);
    builder.ExitBlock();
    String text = builder.ToString();
    text.Ensure16Bit();
    const bool consistent = mapping_builder_.SetDestinationString(text);
    assert(consistent);
    (void)consistent;
    result_->mapping_ = mapping_builder_.Build(*context_.root_);
    for (const auto& item : result_->inline_items_) styles_.push_back(item.object->LayoutStyle());
    SegmentAndShape();
  } else {
    ReuseCollectedItems(*context_.fragments_);
  }
  const String& text = result_->TextContent();
  unsigned start = ReuseLines();
  const auto* locale = context_.root_->Style().GetFont()->GetFontDescription().Locale();
  LazyLineBreakIterator breaks(text, locale, options_.word_break);
  breaks.SetStrictness(options_.line_break);
  // LineBreaker::SetCurrentStyle() sets this per item style; one iterator
  // serves the whole context here, so the root style decides.
  breaks.SetBreakSpace(context_.root_->Style().ShouldBreakSpaces() ? BreakSpaceType::kAfterEverySpace
                                                                   : BreakSpaceType::kAfterSpaceRun);
  CharacterBreakIterator graphemes{StringView(text)};
  while (start < text.length()) {
    unsigned limit = start;
    while (limit < text.length() && !IsForcedBreak(text[limit])) ++limit;
    unsigned end = limit;
    if (options_.wrap && start < limit) {
      unsigned fitting = start;
      for (unsigned candidate = std::min(breaks.NextBreakOpportunity(start + 1), limit);;) {
        if (Measure(start, candidate) > options_.available_inline_size) {
          end = fitting == start ? candidate : fitting;
          if (fitting == start && options_.break_long_words) {
            unsigned last = start;
            for (int next = graphemes.Following(static_cast<int>(start)); next != kTextBreakDone &&
                                                                          static_cast<unsigned>(next) <= candidate;
                 next = graphemes.Following(next)) {
              if (last != start && Measure(start, static_cast<unsigned>(next)) > options_.available_inline_size) break;
              last = static_cast<unsigned>(next);
            }
            if (last != start) end = last;
          }
          break;
        }
        fitting = candidate;
        end = fitting;
        if (candidate == limit) break;
        const unsigned next = std::min(breaks.NextBreakOpportunity(candidate + 1), limit);
        assert(next > candidate);
        candidate = next;
      }
    }
    const bool forced = end == limit && limit < text.length();
    if (forced) {
      ++end;
      if (text[limit] == '\r' && end < text.length() && text[end] == '\n') ++end;
    }
    assert(end > start);
    PlaceLine(start, end, !forced && end < text.length());
    start = end;
  }
  if (text.empty() || IsForcedBreak(text[text.length() - 1])) PlaceLine(start, start, false);
  ToPhysicalCoordinates();
  result_->FinalizeAfterLayout();
  result_->runs_ = std::move(runs_);
  result_->levels_ = std::move(levels_);
  return std::move(result_);
}

} // namespace bkfont
