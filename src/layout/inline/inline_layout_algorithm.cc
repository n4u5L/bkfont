// Adapts Blink InlineNode, ShapingLineBreaker and LogicalLineBuilder to a
// direct model. No block formatting, DOM or fragmentainers.
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_layout_algorithm.h"

#include <algorithm>
#include <cassert>
#include <span>
#include <unicode/uchar.h>

#include "base/notreached.h"
#include "base/text/string_builder.h"
#include "font/font_selector.h"
#include "text/bidi_paragraph.h"
#include "base/text/character_names.h"
#include "geometry/length_functions.h"
#include "layout/inline/inline_box_state.h"
#include "layout/inline_text_metrics.h"
#include "shaping/harfbuzz_shaper.h"
#include "shaping/shape_result_spacing.h"
#include "shaping/shaping_line_breaker.h"
#include "shaping/text_spacing_trim.h"
#include "text/character.h"

namespace bkit {
namespace {

bool IsForcedBreak(UChar c) {
  return c == uchar::kLineFeed;
}
bool IsIgnoredControl(UChar c) {
  return c == uchar::kCarriageReturn || c == uchar::kFormFeed;
}
bool IsControl(UChar c) {
  return IsForcedBreak(c) || c == uchar::kTab;
}
bool IsBidiTrailingSpace(UChar c) {
  return LazyLineBreakIterator::IsBreakableSpace(c) || u_charDirection(c) == U_WHITE_SPACE_NEUTRAL;
}
bool IsLineFeed(UChar c) {
  return c == uchar::kLineFeed;
}
// Items that create no fragments: ignored CR/FF controls and the bidi
// controls injected for unicode-bidi.
bool IsNonFragmentItem(const InlineItem& item, const String& text) {
  return item.type == InlineItem::kBidiControl ||
         (item.type == InlineItem::kControl && item.start < item.end && IsIgnoredControl(text[item.start]));
}

// LineBreaker.cc StrictnessFromLineBreak().
LineBreakStrictness StrictnessFromLineBreak(LineBreak line_break) {
  switch (line_break) {
    case LineBreak::kAuto:
    case LineBreak::kAfterWhiteSpace:
    case LineBreak::kAnywhere: return LineBreakStrictness::kDefault;
    case LineBreak::kNormal: return LineBreakStrictness::kNormal;
    case LineBreak::kStrict: return LineBreakStrictness::kStrict;
    case LineBreak::kLoose: return LineBreakStrictness::kLoose;
  }
  NOTREACHED();
}

// The break type LineBreaker::SetCurrentStyleForce() selects.
LineBreakType LineBreakTypeFor(const ComputedStyle& style) {
  if (style.GetLineBreak() == LineBreak::kAnywhere) return LineBreakType::kBreakCharacter;
  switch (style.WordBreak()) {
    case EWordBreak::kNormal:
    case EWordBreak::kBreakWord: return LineBreakType::kNormal;
    case EWordBreak::kBreakAll: return LineBreakType::kBreakAll;
    case EWordBreak::kKeepAll: return LineBreakType::kKeepAll;
    case EWordBreak::kAutoPhrase: return LineBreakType::kPhrase;
  }
  NOTREACHED();
}

// LineBreaker::break_anywhere_if_overflow_ in LineBreakerMode::kContent.
bool BreakAnywhereIfOverflow(const ComputedStyle& style) {
  if (!style.ShouldWrapLine() || style.GetLineBreak() == LineBreak::kAnywhere) return false;
  return style.WordBreak() == EWordBreak::kBreakWord || style.OverflowWrap() != EOverflowWrap::kNormal;
}

// LineBreaker::SetCurrentStyleForce(): configures `breaks` for `style`.
// `break_anywhere` is override_break_anywhere_.
void ConfigureBreakIterator(LazyLineBreakIterator& breaks, const ComputedStyle& style, bool break_anywhere) {
  breaks.SetLocale(style.GetFontDescription().Locale());
  Hyphens hyphens = style.GetHyphens();
  if (style.GetLineBreak() == LineBreak::kAnywhere) {
    breaks.SetStrictness(LineBreakStrictness::kDefault);
    breaks.SetBreakType(LineBreakType::kBreakCharacter);
  } else {
    breaks.SetStrictness(StrictnessFromLineBreak(style.GetLineBreak()));
    LineBreakType type = LineBreakTypeFor(style);
    if (type == LineBreakType::kPhrase) hyphens = Hyphens::kNone;
    if (break_anywhere && BreakAnywhereIfOverflow(style)) type = LineBreakType::kBreakCharacter;
    breaks.SetBreakType(type);
  }
  breaks.EnableSoftHyphen(hyphens != Hyphens::kNone);
  breaks.SetBreakSpace(style.ShouldBreakSpaces() ? BreakSpaceType::kAfterEverySpace : BreakSpaceType::kAfterSpaceRun);
}

using RunSegmenterRange = RunSegmenter::RunSegmenterRange;

// InlineItemSegments::Ranges(): locate a shaping window without scanning the
// text again or copying the segments. The stored ranges are in logical order.
std::span<const RunSegmenterRange> SegmentsForRange(std::span<const RunSegmenterRange> segments,
                                                 unsigned start, unsigned end) {
  if (start == end) return {};
  const auto first = std::lower_bound(segments.begin(), segments.end(), start,
                                     [](const RunSegmenterRange& range, unsigned offset) { return range.end <= offset; });
  const auto last = std::lower_bound(first, segments.end(), end,
                                   [](const RunSegmenterRange& range, unsigned offset) { return range.start < offset; });
  return {first, last};
}

// LineBreaker::ShapeLineAt's edge reshaping uses the same segmented input and
// spacing as the initial shape. Keeping this adapter here avoids a second
// shaper in the editing/painting paths.
class LineShaper final : public ShapingLineBreaker {
public:
  LineShaper(const String& text, const Font& font, const ShapeResult& shape,
             const LazyLineBreakIterator& breaks, std::span<const RunSegmenterRange> segments)
      : ShapingLineBreaker(&shape, &breaks, nullptr, &font),
        text_(text),
        font_(font),
        direction_(shape.Direction()),
        segments_(segments) {
  }

private:
  std::shared_ptr<const ShapeResult> Shape(unsigned start, unsigned end, ShapeOptions options) override {
    HarfBuzzShaper shaper(text_);
    auto shape = shaper.Shape(&font_, direction_, start, end, SegmentsForRange(segments_, start, end), options);
    ShapeResultSpacing<String> spacing(text_);
    if (spacing.SetSpacing(font_.GetFontDescription())) shape->ApplySpacing(spacing);
    return shape;
  }
  const String& text_;
  const Font& font_;
  TextDirection direction_;
  std::span<const RunSegmenterRange> segments_;
};

} // namespace

InlineLayoutAlgorithm::InlineLayoutAlgorithm(InlineFormattingContext& context)
    : context_(context),
      options_(context.Options()),
      result_(std::make_unique<FragmentItems>()) {
}

// InlineNode::CollectInlinesInternal(): containers contribute zero-length
// open/close items so the line breaker can switch styles at their boundaries.
// LayoutTextCombine: in a vertical writing mode, the outermost box with
// 'text-combine-upright: all' combines all of its text into one atomic box.
// ComputedStyle::HasTextCombine().
static bool HasTextCombine(const ComputedStyle& style) {
  return style.TextCombine() != ETextCombine::kNone && !IsHorizontalWritingMode(style.GetWritingMode());
}

static void AppendTextContent(const InlineObject& object, StringBuilder& builder) {
  if (object.IsText()) builder.Append(object.Text());
  for (const auto& child : object.Children()) AppendTextContent(*child, builder);
}

void InlineLayoutAlgorithm::Collect(const InlineObject& object, InlineItemsBuilder& builder) {
  if (object.Parent() && !object.IsAtomicInline() && HasTextCombine(object.Style()) &&
      (!object.Parent()->Parent() || !HasTextCombine(object.Parent()->Style()))) {
    StringBuilder text;
    AppendTextContent(object, text);
    if (text.empty()) return;
    const ComputedStyle& style = object.Style();
    const TextDecorationLine decorations = style.TextDecorationsInEffect();
    FontSelector* selector = style.GetFont()->GetFontSelector();
    result_->text_combines_.Set(
        &object, std::make_shared<const TextCombine>(
                     text.ToString(), *style.GetFont(), selector ? selector->shared_from_this() : nullptr,
                     style.Direction(),
                     (decorations & (TextDecorationLine::kUnderline | TextDecorationLine::kOverline)) !=
                         TextDecorationLine::kNone));
    builder.AppendAtomicInline(object);
    return;
  }
  if (object.IsInline()) {
    if (object.Parent()) builder.EnterInline(object);
    for (const auto& child : object.Children()) Collect(*child, builder);
    if (object.Parent()) builder.ExitInline(object);
    return;
  }
  if (object.IsText()) {
    // The local model permits declarations on text objects. A text object
    // with its own style inputs is an inline box around its text; Blink's
    // LayoutText gets its style from its enclosing LayoutInline instead.
    const bool needs_scope = object.Parent() && object.HasOwnStyle();
    if (needs_scope) builder.EnterInline(object);
    builder.AppendText(object);
    if (needs_scope) builder.ExitInline(object);
  } else {
    builder.AppendAtomicInline(object);
  }
}

namespace {

// ReusingTextShaper (inline_node.cc): shapes a run, reusing the shape results
// of the previous layout for the text a text edit left as it was, as
// InlineNode::SetTextWithOffset() does. The text kept is the common prefix and
// suffix of the old and new text content (InlineNodeDataEditor's
// MatchedLengths()). A shape result is cut only at offsets that are safe to
// break, away from the edit (GetFirstSafeToReuse(), GetLastSafeToReuse()).
//
// As upstream, partial reuse is disabled when InlineItemSegments is needed.
// The remaining candidates must retain their bidi level and shaping context.
class ReusingTextShaper {
public:
  struct Previous {
    const String& text;
    std::span<const InlineItemRun> runs;
    const Vector<UBiDiLevel>& levels;
    const Vector<uint32_t>& shaping_context;
  };

  ReusingTextShaper(const HarfBuzzShaper& shaper, const String& text, const Vector<UBiDiLevel>& levels,
                    const Vector<uint32_t>& shaping_context, std::optional<Previous> previous)
      : shaper_(shaper), text_(text), levels_(levels), shaping_context_(shaping_context), previous_(previous) {
    if (!previous_) return;
    const String& old_text = previous_->text;
    const unsigned old_length = old_text.length(), new_length = text.length();
    const unsigned limit = std::min(old_length, new_length);
    while (prefix_ < limit && old_text[prefix_] == text[prefix_]) ++prefix_;
    while (suffix_ < limit - prefix_ && old_text[old_length - 1 - suffix_] == text[new_length - 1 - suffix_]) ++suffix_;
  }

  std::shared_ptr<ShapeResult> Shape(const InlineItemRun& run,
                                     std::span<const RunSegmenterRange> ranges) const {
    const Font* font = run.style->GetFont();
    const TextDirection direction = DirectionFromLevel(run.level);
    ShapeOptions options = run.shape_options;
    Vector<Span> spans;
    if (previous_) {
      // The prefix keeps its offsets; the suffix moves by the length change.
      const unsigned new_length = text_.length();
      const int delta = static_cast<int>(new_length) - static_cast<int>(previous_->text.length());
      if (run.start < prefix_) CollectSpans(run, *font, run.start, std::min(run.end, prefix_), 0, spans);
      if (run.end > new_length - suffix_)
        CollectSpans(run, *font, std::max(run.start, new_length - suffix_), run.end, delta, spans);
    }
    if (spans.empty()) return shaper_.Shape(font, direction, run.start, run.end, ranges, options);

    std::shared_ptr<ShapeResult> result = ShapeResult::CreateEmpty(*spans.front().old_run->shape);
    unsigned offset = run.start;
    for (const Span& span : spans) {
      if (offset < span.start) {
        Append(*Reshape(font, direction, offset, span.start, ranges, options), result.get());
        options.han_kerning_start = false;
      }
      const ShapeResult& shape = *span.old_run->shape;
      if (span.old_start == span.start) {
        shape.CopyRange(span.start, span.end, result.get());
      } else {
        // InlineNodeDataEditor::ShiftItem().
        shape.SubRange(span.old_start, span.old_end)->CopyAdjustedOffset(span.start)->CopyRange(span.start, span.end,
                                                                                              result.get());
      }
      offset = span.end;
    }
    if (offset < run.end) Append(*Reshape(font, direction, offset, run.end, ranges, options), result.get());
    return result;
  }

private:
  // New offsets [start, end) shaped as [old_start, old_end) of `old_run`.
  struct Span {
    unsigned start;
    unsigned end;
    const InlineItemRun* old_run;
    unsigned old_start;
    unsigned old_end;
  };

  // The spans of the new offsets [start, end) of `run`, which are the old
  // offsets less `delta`, that old runs can give.
  void CollectSpans(const InlineItemRun& run, const Font& font, unsigned start, unsigned end, int delta,
                    Vector<Span>& spans) const {
    const unsigned old_start = start - delta, old_end = end - delta;
    const std::span<const InlineItemRun> old_runs = previous_->runs;
    auto it = std::lower_bound(old_runs.begin(), old_runs.end(), old_start,
                               [](const InlineItemRun& old_run, unsigned offset) { return old_run.end <= offset; });
    for (; it != old_runs.end() && it->start < old_end; ++it) {
      const InlineItemRun& old_run = *it;
      // ReusingTextShaper::CollectReusableShapeResults().
      if (old_run.control || !old_run.shape || old_run.unsafe_to_reuse_shape || old_run.shape->IsAppliedSpacing() ||
          old_run.level != run.level || !(*old_run.style->GetFont() == font) ||
          old_run.shape_options.is_line_start != run.shape_options.is_line_start ||
          old_run.shape_options.han_kerning_start != run.shape_options.han_kerning_start) {
        continue;
      }
      // Split where the bidi level or the run segment differs.
      unsigned from = std::max(old_start, old_run.start);
      const unsigned to = std::min(old_end, old_run.end);
      while (from < to) {
        while (from < to && !SameShaping(from, delta)) ++from;
        unsigned until = from;
        while (until < to && SameShaping(until, delta)) ++until;
        if (from < until) AddSpan(run, old_run, from, until, delta, spans);
        from = until;
      }
    }
  }

  bool SameShaping(unsigned old_offset, int delta) const {
    const unsigned offset = old_offset + delta;
    return previous_->levels[old_offset] == levels_[offset] &&
           previous_->shaping_context[old_offset] == shaping_context_[offset];
  }

  // Cuts the old shape result at offsets safe to break, unless the old and the
  // new runs both start (or end) there.
  void AddSpan(const InlineItemRun& run, const InlineItemRun& old_run, unsigned old_start, unsigned old_end, int delta,
               Vector<Span>& spans) const {
    const ShapeResult& shape = *old_run.shape;
    if (old_start != old_run.start || old_start + delta != run.start) {
      old_start = FirstSafeToReuse(shape, old_start, old_end);
    }
    if (old_end != old_run.end || old_end + delta != run.end) {
      old_end = LastSafeToReuse(shape, old_start, old_end);
    }
    if (old_start >= old_end) return;
    spans.push_back(Span{old_start + delta, old_end + delta, &old_run, old_start, old_end});
  }

  // InlineNodeDataEditor::GetFirstSafeToReuse(): the first glyph after a cut
  // may kern or join with the text before it.
  static unsigned FirstSafeToReuse(const ShapeResult& shape, unsigned start, unsigned end) {
    // TODO(yosin): It is better to utilize OpenType |usMaxContext|.
    // For font having "fi", |usMaxContext = 2".
    constexpr unsigned kSkip = 2 - 1;
    if (start + kSkip >= end) return end;
    shape.EnsurePositionData();
    return std::min(end, shape.CachedNextSafeToBreakOffset(start + kSkip));
  }

  // InlineNodeDataEditor::GetLastSafeToReuse().
  unsigned LastSafeToReuse(const ShapeResult& shape, unsigned start, unsigned end) const {
    // For font having "fi", usMaxContext = 2.
    // For Emoji with ZWJ, usMaxContext = 10. (http://crbug.com/1213235)
    const unsigned skip = (text_.Is8Bit() ? 2 : 10) - 1;
    if (end <= start + skip) return start;
    shape.EnsurePositionData();
    return std::max(start, shape.CachedPreviousSafeToBreakOffset(end - skip));
  }

  std::shared_ptr<ShapeResult> Reshape(const Font* font, TextDirection direction, unsigned start, unsigned end,
                                       std::span<const RunSegmenterRange> ranges, ShapeOptions options) const {
    return shaper_.Shape(font, direction, start, end, SegmentsForRange(ranges, start, end), options);
  }

  // ReusingTextShaper::AppendShapeResult().
  static void Append(const ShapeResult& shape, ShapeResult* target) {
    shape.CopyRange(shape.StartIndex(), shape.EndIndex(), target);
  }

  const HarfBuzzShaper& shaper_;
  const String& text_;
  const Vector<UBiDiLevel>& levels_;
  const Vector<uint32_t>& shaping_context_;
  const std::optional<Previous> previous_;
  unsigned prefix_ = 0;
  unsigned suffix_ = 0;
};

} // namespace

// InlineNode::SegmentScriptRuns()/SegmentFontOrientation(): script and emoji
// segmentation needs the whole text, while mixed orientation is item-local.
bool InlineLayoutAlgorithm::SegmentText(bool use_latin1_script) {
  const String& text = result_->TextContent();
  auto& segments = result_->segments_;
  assert(segments.empty());
  std::optional<RunSegmenter> segmenter;
  RunSegmenterRange script_segment{0, text.length(), USCRIPT_LATIN,
                                  OrientationIterator::kOrientationKeep, FontFallbackPriority::kText};
  if (!use_latin1_script) {
    segmenter.emplace(text.Span16(), FontOrientation::kHorizontal);
    if (!segmenter->Consume(&script_segment)) NOTREACHED();
  }
  bool has_segmented_text = script_segment.end < text.length();
  unsigned offset = 0;
  const auto append_until = [&](unsigned end, OrientationIterator::RenderOrientation orientation) {
    while (offset < end) {
      if (offset == script_segment.end && !segmenter->Consume(&script_segment)) NOTREACHED();
      RunSegmenterRange segment = script_segment;
      segment.start = offset;
      segment.end = std::min(end, script_segment.end);
      segment.render_orientation = orientation;
      // PopulateItemsFromFontOrientation() preserves every mixed item's
      // start/end boundary, even when the adjacent segment has the same data.
      segments.push_back(segment);
      offset = segment.end;
    }
  };
  if (context_.root_->Style().IsHorizontalTypographicMode()) {
    append_until(text.length(), OrientationIterator::kOrientationKeep);
    return has_segmented_text;
  }
  const auto& items = result_->inline_items_;
  for (size_t i = 0; i < items.size(); ++i) {
    const InlineItem& item = items[i];
    if (item.type != InlineItem::kText || item.start == item.end ||
        styles_[i]->GetFontDescription().Orientation() != FontOrientation::kVerticalMixed) {
      continue;
    }
    // SegmentFontOrientation() creates InlineItemSegments even when the
    // entire mixed-vertical item has one script and one orientation.
    has_segmented_text = true;
    append_until(item.start, OrientationIterator::kOrientationKeep);
    // Upstream splits items in SegmentBidiRuns() before orientation. Our
    // collected items stay intact, so reproduce those boundaries here.
    for (unsigned start = item.start; start < item.end;) {
      unsigned end = start + 1;
      while (end < item.end && levels_[end] == levels_[start]) ++end;
      OrientationIterator orientation_iterator(text.Span16().subspan(start, end - start),
                                               FontOrientation::kVerticalMixed);
      unsigned orientation_end;
      OrientationIterator::RenderOrientation orientation;
      while (orientation_iterator.Consume(&orientation_end, &orientation)) {
        append_until(start + orientation_end, orientation);
      }
      start = end;
    }
  }
  append_until(text.length(), OrientationIterator::kOrientationKeep);
  return has_segmented_text;
}

void InlineLayoutAlgorithm::SegmentAndShape(bool use_latin1_script, bool is_bidi_enabled) {
  const String& text = result_->TextContent();
  const ComputedStyle& block_style = context_.root_->Style();
  levels_ = Vector<UBiDiLevel>(text.length(), 0);
  result_->shaping_context_.resize(text.length());
  if (text.empty()) return;
  BidiParagraph bidi;
  // InlineNode::SegmentBidiRuns(): 'unicode-bidi: plaintext' determines the
  // paragraph level from the content.
  const std::optional<TextDirection> base_direction =
      block_style.GetUnicodeBidi() == UnicodeBidi::kPlaintext ? std::nullopt
                                                              : std::optional(block_style.Direction());
  if (is_bidi_enabled) {
    if (!bidi.SetParagraph(text, base_direction) || (bidi.IsUnidirectional() && IsLtr(bidi.BaseDirection()))) {
      // InlineNode::SegmentBidiRuns(): failure or entirely LTR text disables
      // bidi and permits the Latin-1 script fast path.
      is_bidi_enabled = false;
    } else {
      for (unsigned start = 0; start < text.length();) {
        UBiDiLevel level;
        const unsigned end = bidi.GetLogicalRun(start, &level);
        std::fill(levels_.begin() + start, levels_.begin() + end, level);
        start = end;
      }
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
  const bool has_segmented_text = SegmentText(use_latin1_script && !is_bidi_enabled);
  HarfBuzzShaper shaper(text);
  std::optional<ReusingTextShaper::Previous> previous;
  if (!has_segmented_text && context_.reuse_shape_results_ && context_.fragments_) {
    const FragmentItems& old = *context_.fragments_;
    previous.emplace(ReusingTextShaper::Previous{old.TextContent(), {old.runs_.data(), old.runs_.size()}, old.levels_,
                                                 old.shaping_context_});
  }
  const ReusingTextShaper reusing_shaper(shaper, text, levels_, result_->shaping_context_, previous);
  const std::span<const RunSegmenterRange> segments(result_->segments_.data(), result_->segments_.size());
  size_t segment_index = 0;
  bool is_next_start_of_paragraph = true;
  for (Run& run : runs_) {
    if (run.control) {
      // Empty items are opaque to text processing and have no runs. Every
      // nonempty non-text item resets this state, including bidi controls.
      is_next_start_of_paragraph = IsForcedBreak(text[run.start]);
      continue;
    }
    const FontDescription& font_description = run.style->GetFontDescription();
    run.shape_options = {
        .is_line_start = is_next_start_of_paragraph,
        .han_kerning_start = is_next_start_of_paragraph &&
                             ShouldTrimStartOfParagraph(font_description.GetTextSpacingTrim()) &&
                             Character::MaybeHanKerningOpen(text[run.start]),
    };
    is_next_start_of_paragraph = false;
    // Runs and segments are ordered, so initial shaping needs only one walk.
    while (segments[segment_index].end <= run.start) ++segment_index;
    const size_t first_segment = segment_index;
    while (segments[segment_index].end < run.end) ++segment_index;
    const auto ranges = segments.subspan(first_segment, segment_index - first_segment + 1);
    for (const auto& segment : ranges) {
      const uint32_t key = static_cast<uint32_t>(segment.script) |
                           (static_cast<uint32_t>(segment.render_orientation) << 16) |
                           (static_cast<uint32_t>(segment.font_fallback_priority) << 24);
      std::fill(result_->shaping_context_.begin() + std::max(run.start, segment.start),
                result_->shaping_context_.begin() + std::min(run.end, segment.end), key);
    }
    run.shape = reusing_shaper.Shape(run, ranges);
    ShapeResultSpacing<String> spacing(text);
    if (spacing.SetSpacing(run.style->GetFont()->GetFontDescription())) run.shape->ApplySpacing(spacing);
  }
}

// TextAutoSpace::Apply() (text_auto_space.cc): inserts the
// 'text-autospace' spacing between ideographs and non-ideographic letters or
// numerals. SpacingApplier assigns each opportunity to an item; the spacing
// is applied to the shaping run that holds the item's text.
// https://drafts.csswg.org/css-text-4/#propdef-text-autospace
void InlineLayoutAlgorithm::ApplyTextAutoSpace() {
  const String& text = result_->TextContent();
  const auto& items = result_->inline_items_;
  // TextAutoSpace::TextAutoSpace(): MayApply().
  if (std::none_of(items.begin(), items.end(), [](const InlineItem& item) {
        return item.type == InlineItem::kText && item.start != item.end &&
               item.object->Style().TextAutospace() != ETextAutospace::kNoAutospace;
      }))
    return;
  bool may_need = false;
  for (unsigned i = 0; i < text.length() && !may_need; ++i) may_need = Character::MayNeedEastAsianSpacing(text[i]);
  if (!may_need) return;

  // SpacingApplier state.
  wtf_size_t item_index = 0;
  bool is_disabled = false;
  bool is_disabled_by_style = false;
  bool is_last_disabled = false;
  const ComputedStyle* style = nullptr;
  const auto did_change_item = [&] {
    const InlineItem& item = items[item_index];
    if (item.start == item.end) return;
    // Bidi controls have no layout object upstream.
    if (item.type == InlineItem::kBidiControl) {
      is_disabled = true;
      return;
    }
    const ComputedStyle* item_style = &item.object->Style();
    if (item_style != style) {
      style = item_style;
      // Upright non-ideographic characters are `kOther`.
      // https://drafts.csswg.org/css-text-4/#non-ideographic-letters
      is_disabled_by_style = style->TextAutospace() != ETextAutospace::kNormal ||
                             style->GetFontDescription().Orientation() == FontOrientation::kVerticalUpright;
    }
    // Only text items have shape results.
    is_disabled = is_disabled_by_style || item.type != InlineItem::kText;
  };
  const auto advance_item = [&] {
    is_last_disabled = is_disabled;
    ++item_index;
    did_change_item();
  };
  const auto item_end = [&] { return items[item_index].end; };
  const auto is_offset_disabled = [&](unsigned offset) {
    return is_last_disabled && offset == items[item_index].start;
  };
  // Offsets with the item index that owns them.
  struct Opportunity {
    unsigned offset;
    wtf_size_t item;
  };
  Vector<Opportunity> opportunities;
  const auto insert_space_before = [&](unsigned offset) {
    if (offset < item_end()) {
      opportunities.push_back(Opportunity{offset, item_index});
      return;
    }
    // If the `offset` is at the boundary, add to the earlier item if it's LTR.
    const wtf_size_t last_item = item_index;
    const bool is_offset_for_last_item =
        offset == item_end() && IsLtr(DirectionFromLevel(levels_[items[last_item].start]));
    while (offset >= item_end() && item_index + 1 < items.size()) advance_item();
    if (is_disabled || is_offset_disabled(offset)) [[unlikely]] {
      return;
    }
    opportunities.push_back(Opportunity{offset, is_offset_for_last_item ? last_item : item_index});
  };
  bool started = false;
  const auto begin_applier = [&](unsigned offset) {
    started = true;
    did_change_item();
    while (offset > item_end() && item_index + 1 < items.size()) advance_item();
    if (!is_disabled && !is_offset_disabled(offset)) insert_space_before(offset);
  };

  const bool is_chinese = [&] {
    const LayoutLocale* locale = context_.root_->Style().GetFontDescription().Locale();
    return locale && locale->IsMacrolanguageChinese();
  }();
  // Resolve UTR#59: East Asian Spacing "Conditional".
  const auto resolve_conditional = [&](EastAsianSpacingType type) {
    if (type != EastAsianSpacingType::kConditional) return type;
    return is_chinese ? EastAsianSpacingType::kNarrow : EastAsianSpacingType::kOther;
  };
  EastAsianSpacingType last_type = EastAsianSpacingType::kOther;
  bool is_last_wide = false;
  for (unsigned i = 0; i < text.length();) {
    unsigned next = i;
    UChar32 ch;
    U16_NEXT(text.Span16().data(), next, text.length(), ch);
    EastAsianSpacingType type = Character::GetEastAsianSpacingType(ch);
    const bool is_wide = type == EastAsianSpacingType::kWide;
    if (is_wide || is_last_wide) [[unlikely]] {
      if (Character::IsGcMark(ch)) [[unlikely]] {
        i = next;
        continue;
      }
      type = resolve_conditional(type);
      last_type = resolve_conditional(last_type);
      const bool needs_space = (is_last_wide && type == EastAsianSpacingType::kNarrow) ||
                               (is_wide && last_type == EastAsianSpacingType::kNarrow);
      if (needs_space) [[unlikely]] {
        if (!started) begin_applier(i);
        else insert_space_before(i);
        if (is_disabled) [[unlikely]] {
          i = std::max(i + 1, static_cast<unsigned>(item_end()));
          last_type = EastAsianSpacingType::kOther;
          is_last_wide = false;
          continue;
        }
      }
    }
    last_type = type;
    is_last_wide = is_wide;
    i = next;
  }
  if (opportunities.empty()) return;

  // SpacingApplier::Apply(): the spacing of the owning item's font, applied to
  // the run of the item's text.
  for (Run& run : runs_) {
    if (!run.shape) continue;
    Vector<OffsetWithSpacing, 16> offsets;
    for (const Opportunity& opportunity : opportunities) {
      const InlineItem& item = items[opportunity.item];
      // An opportunity at the end of its item belongs to the run before it.
      const unsigned position = opportunity.offset == item.end ? opportunity.offset - 1 : opportunity.offset;
      if (position < run.start || position >= run.end) continue;
      const float spacing = item.object->Style().GetFont()->TextAutoSpaceInlineSize();
      if (!offsets.empty() && offsets.back().offset == opportunity.offset) continue;
      offsets.push_back(OffsetWithSpacing{opportunity.offset, spacing});
    }
    if (!offsets.empty()) {
      run.shape->ApplyTextAutoSpacing(offsets);
      run.unsafe_to_reuse_shape = true;
    }
  }
}

// InlineNode::PrepareLayoutIfNeeded() keeps InlineNodeData when only layout
// inputs changed: text, offset mapping, bidi levels and shape results stay.
// Styles are refreshed; any change that needs reshaping (including font,
// spacing and text edits) sets NeedsCollectInlines instead.
void InlineLayoutAlgorithm::ReuseCollectedItems(const FragmentItems& old) {
  result_->mapping_ = old.mapping_;
  result_->inline_items_ = old.inline_items_;
  result_->text_combines_ = old.text_combines_;
  result_->shaping_context_ = old.shaping_context_;
  result_->segments_ = old.segments_;
  levels_ = old.levels_;
  runs_ = old.runs_;
  for (const auto& item : result_->inline_items_) styles_.push_back(item.object->LayoutStyle());
  for (Run& run : runs_) run.style = run.object->LayoutStyle();
}

HeapVector<FragmentItem> InlineLayoutAlgorithm::ShapeLine(unsigned start, unsigned end, bool hyphenated) {
  const String& text = result_->TextContent();
  HeapVector<FragmentItem> items;
  // LineBreaker::PrepareNextLine(): 'text-indent' is the initial position, so
  // that tab positions align regardless of it.
  LayoutUnit advance = TextIndent(start);
  // HandleText() drops a leading collapsible space when a break at an inline
  // boundary left it for the next line instead of consuming it as trailing.
  unsigned content_start = start;
  for (const auto& item : result_->inline_items_) {
    if (item.end <= content_start || item.start == item.end) continue;
    if (item.start >= end) break;
    if (item.is_generated_for_line_break || IsNonFragmentItem(item, text)) {
      content_start = std::min(item.end, end);
      continue;
    }
    if (item.type == InlineItem::kText && item.object->Style().ShouldCollapseWhiteSpaces() &&
        text[content_start] == uchar::kSpace) {
      ++content_start;
      if (content_start == item.end) continue;
    }
    break;
  }
  if (content_start == end) return items;
  unsigned trailing_space = end;
  while (trailing_space > start && (IsBidiTrailingSpace(text[trailing_space - 1]) || IsForcedBreak(text[trailing_space - 1]))) --trailing_space;
  for (const Run& run : runs_) {
    if (run.end <= content_start) continue;
    if (run.start >= end) break;
    const unsigned run_start = std::max(content_start, run.start);
    const unsigned run_end = std::min(end, run.end);
    // HandleControlItem ignores CR/FF; they keep identity offset mappings.
    if (run.control && IsIgnoredControl(text[run_start])) continue;
    std::shared_ptr<const ShapeResultView> shape;
    if (run.shape) {
      LazyLineBreakIterator breaks(text, run.style->GetFont()->GetFontDescription().Locale(),
                                   LineBreakTypeFor(*run.style));
      LineShaper shaper(text, *run.style->GetFont(), *run.shape, breaks,
                        {result_->segments_.data(), result_->segments_.size()});
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
      if (unit.is_generated_for_line_break || unit.type == InlineItem::kBidiControl) continue;
      const unsigned unit_end = std::min(run_end, unit.end);
      const bool split_trailing = run.level != (IsLtr(base_direction_) ? 0 : 1) &&
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
        } else if (unit.type == InlineItem::kAtomicInline) {
          // A LayoutTextCombine box: 1em in the (vertical) inline direction.
          item.type_ = FragmentItem::kBox;
          item.text_combine_ = result_->text_combines_.at(unit.object);
          item.inline_size_ = item.text_combine_->Size().height;
        }
        advance += item.inline_size_;
        items.push_back(std::move(item));
        fragment_start = fragment_end;
      }
    }
  }
  RemoveTrailingCollapsibleSpace(items, start, end);
  // HyphenResult::Shape and LogicalLineBuilder::PlaceHyphen. A soft hyphen
  // generates a visible hyphen only when it actually ends a wrapped line, and
  // 'hyphens: auto' when the line breaker hyphenated the last word.
  const bool soft_hyphen =
      end > start && end < text.length() && text[end - 1] == uchar::kSoftHyphen && !IsForcedBreak(text[end]);
  if (!items.empty() && !items.back().IsAtomicInline() && (hyphenated || soft_hyphen)) {
    FragmentItem hyphen;
    const auto& previous = items.back();
    hyphen.object_ = previous.object_;
    hyphen.style_ = previous.style_;
    hyphen.bidi_level_ = previous.bidi_level_;
    hyphen.type_ = FragmentItem::kGeneratedText;
    hyphen.text_offset_ = {end, end};
    hyphen.generated_text_ = hyphen.Style().HyphenString().GetString();
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
void InlineLayoutAlgorithm::RemoveTrailingCollapsibleSpace(HeapVector<FragmentItem>& items,
                                                          unsigned start, unsigned end) const {
  const String& text = result_->TextContent();
  // Ignored CR/FF control items have no fragments, but upstream retains their
  // empty line results. Unlike forced breaks, these stop trailing collapse.
  for (wtf_size_t i = result_->inline_items_.size(); i;) {
    const auto& item = result_->inline_items_[--i];
    if (item.end <= start) break;
    if (item.start >= end || item.start == item.end || item.is_generated_for_line_break ||
        item.type == InlineItem::kBidiControl)
      continue;
    if (item.type == InlineItem::kControl) {
      if (IsForcedBreak(text[item.start])) continue;
      if (IsIgnoredControl(text[item.start])) return;
    }
    break;
  }
  for (wtf_size_t i = items.size(); i;) {
    FragmentItem& item = items[--i];
    if (item.IsAtomicInline()) return;
    // A preserved newline may belong to a different node/style than the
    // collapsible space before it. Blink searches past forced breaks.
    if (item.IsLineBreak()) continue;
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

// LineInfo::ComputeTrailingSpaceWidth(): preserve hangs at soft wraps. On the
// last line or before a forced break, only overflowing trailing spaces hang.
// Tabs are control items upstream and participate in the same calculation.
LayoutUnit InlineLayoutAlgorithm::HangingTrailingSpaceWidth(const HeapVector<FragmentItem>& items,
                                                            LayoutUnit width, bool is_last_line) const {
  const String& text = result_->TextContent();
  LayoutUnit hang;
  for (wtf_size_t i = items.size(); i;) {
    const FragmentItem& item = items[--i];
    if (item.IsGeneratedText() || item.IsAtomicInline()) break;
    if (item.IsLineBreak()) continue;
    const auto& style = item.Style();
    const auto range = item.text_offset_;
    if (range.start == range.end) continue;
    unsigned end = range.end;
    while (end > range.start &&
           (text[end - 1] == uchar::kSpace || text[end - 1] == uchar::kTab ||
            Character::IsOtherSpaceSeparator(text[end - 1])))
      --end;
    if (end == range.end) break;
    bool will_continue = end == range.start;
    const LayoutUnit trailing_width = will_continue || !item.shape_
                                          ? item.inline_size_
                                          : item.inline_size_ - ShapeResultView::Create(item.shape_.get(), range.start, end)->SnappedWidth();
    if (trailing_width) {
      if (style.ShouldBreakSpaces() || (style.ShouldPreserveWhiteSpaces() && !style.ShouldWrapLine())) break;
      if (style.ShouldPreserveWhiteSpaces() && is_last_line && !hang) {
        const LayoutUnit actual_hang = std::min(trailing_width, width - available_width_).ClampNegativeToZero();
        if (actual_hang != trailing_width) will_continue = false;
        hang += actual_hang;
      } else {
        hang += trailing_width;
      }
    }
    if (!will_continue) break;
  }
  return hang;
}

// LineBreaker::NextLine() for the greedy algorithm: the end of the line that
// starts at `start` (before a forced break, which the caller adds), and
// whether it ends with a hyphen.
InlineLayoutAlgorithm::LineBreakResult InlineLayoutAlgorithm::BreakLine(unsigned start, LayoutUnit available_width) {
  const String& text = result_->TextContent();
  LazyLineBreakIterator& breaks = *breaks_;
  LazyLineBreakIterator& emergency_breaks = *emergency_breaks_;
  const LayoutUnit saved_available_width = available_width_;
  available_width_ = available_width;
  unsigned limit = start;
  while (limit < text.length() && !IsForcedBreak(text[limit])) ++limit;
  LineBreakResult result{limit, false};
  if (start < limit) {
    unsigned fitting = start;
    for (unsigned candidate = NextBreakOpportunity(breaks, start + 1, limit);;) {
      bool has_content;
      const LayoutUnit width = Measure(start, candidate, &has_content);
      if (!has_content && candidate < limit) {
        // A generated break opportunity alone must not create an empty line.
        candidate = NextBreakOpportunity(breaks, candidate + 1, limit);
        continue;
      }
      if (width > available_width_) {
        result.end = fitting == start ? candidate : fitting;
        // LineBreaker: 'hyphens: auto' breaks the overflowing word at a
        // hyphenation opportunity before break-anywhere applies.
        if (const unsigned hyphen_end = Hyphenate(start, fitting, candidate); hyphen_end > start) {
          result = {hyphen_end, true};
          break;
        }
        if (fitting == start && BreakAnywhereIfOverflow(StyleAtOffset(start))) {
          unsigned last = start;
          for (unsigned next = NextBreakOpportunity(emergency_breaks, start + 1, candidate, true);;) {
            bool has_emergency_content;
            const LayoutUnit emergency_width = Measure(start, next, &has_emergency_content);
            if (has_emergency_content) {
              if (last != start && emergency_width > available_width_) break;
              last = next;
            }
            if (next == candidate) break;
            next = NextBreakOpportunity(emergency_breaks, next + 1, candidate, true);
          }
          if (last != start) result.end = last;
        }
        break;
      }
      fitting = candidate;
      result.end = fitting;
      if (candidate == limit) break;
      const unsigned next = NextBreakOpportunity(breaks, candidate + 1, limit);
      assert(next > candidate);
      candidate = next;
    }
  }
  available_width_ = saved_available_width;
  return result;
}

// The advance of [from, to) from the shaped runs, without line-edge
// reshaping: LineBreakCandidateContext's position.
float InlineLayoutAlgorithm::ContentWidth(unsigned from, unsigned to) const {
  float width = 0;
  if (from >= to) return width;
  for (const Run& run : runs_) {
    if (run.end <= from) continue;
    if (run.start >= to) break;
    const unsigned run_start = std::max(from, run.start);
    const unsigned run_end = std::min(to, run.end);
    if (run.shape) {
      width += ShapeResultView::Create(run.shape.get(), run_start, run_end)->Width();
    } else if (run.object->IsAtomicInline()) {
      width += run.object->LayoutAtomicSize().inline_size.ToFloat();
    } else if (const auto combine = result_->text_combines_.find(run.object);
               combine != result_->text_combines_.end()) {
      width += combine->value->Size().height.ToFloat();
    } else if (result_->TextContent()[run_start] == uchar::kTab) {
      const auto tab = ShapeResult::CreateForTabulationCharacters(run.style->GetFont(), DirectionFromLevel(run.level),
                                                                  run.style->GetTabSize(), width, run_start, 1);
      width += tab->Width();
    }
  }
  return width;
}

// ScoreLineBreaker::OptimalBreakPoints(): computes `break_points_` for the
// paragraph that starts at `start` when it ends within MaxLines() greedy lines
// and the optimization applies.
bool InlineLayoutAlgorithm::ComputeScoreBreakPoints(unsigned start, bool is_balanced) {
  constexpr wtf_size_t kMaxLinesForBalance = 6;
  constexpr wtf_size_t kMaxLinesForOptimal = 4;
  const wtf_size_t max_lines = is_balanced ? kMaxLinesForBalance : kMaxLinesForOptimal;
  const String& text = result_->TextContent();
  unsigned limit = start;
  while (limit < text.length() && !IsForcedBreak(text[limit])) ++limit;
  if (start >= limit) return false;

  // Greedy lines of the paragraph, up to `max_lines`.
  struct GreedyLine {
    unsigned start;
    unsigned end;
    bool is_hyphenated;
  };
  Vector<GreedyLine> lines;
  for (unsigned line_start = start; line_start < limit;) {
    if (lines.size() >= max_lines) return false;
    const LineBreakResult result = BreakLine(line_start, options_.available_inline_size);
    lines.push_back(GreedyLine{line_start, result.end, result.is_hyphenated});
    if (result.end <= line_start) return false;
    line_start = result.end;
  }
  // Optimization not needed for single line paragraphs.
  if (lines.size() <= 1) return false;
  const ComputedStyle& block_style = context_.root_->Style();
  if (!is_balanced) {
    // ShouldOptimize(): the last line is short and a single word, or the two
    // lines before it are hyphenated.
    const GreedyLine& last_line = lines.back();
    const LayoutUnit last_width = Measure(last_line.start, last_line.end);
    constexpr int kShortLineDenominator = 3;
    const bool can_break_inside =
        NextBreakOpportunity(*breaks_, last_line.start + 1, last_line.end) < last_line.end;
    constexpr wtf_size_t kNumLastHyphenatedLines = 2;
    const wtf_size_t num_lines = lines.size();
    const bool short_last_line = last_width < options_.available_inline_size / kShortLineDenominator && !can_break_inside;
    const bool hyphenated_lines = num_lines >= kNumLastHyphenatedLines + 1 && lines[num_lines - 2].is_hyphenated &&
                                  lines[num_lines - 3].is_hyphenated;
    if (!short_last_line && !hyphenated_lines) return false;
  }

  // SetupParameters(): Minikin's computePenalties() heuristics.
  const LayoutUnit available_width = options_.available_inline_size.ClampNegativeToZero();
  const float font_size = block_style.GetFontDescription().ComputedSize();
  const float zoom = block_style.EffectiveZoom();
  const float width_times_font_size = available_width.ToFloat() * font_size / zoom;
  const bool is_justified = block_style.GetTextAlign() == ETextAlign::kJustify;
  const float hyphen_penalty = is_justified ? width_times_font_size / 2 : width_times_font_size * 2;
  const float line_penalty = is_justified ? .0f : hyphen_penalty * 2;

  // ComputeCandidates(): the break opportunities (and hyphenation
  // opportunities) with their positions and penalties, between sentinels.
  struct Candidate {
    unsigned offset;
    float pos_no_break;
    float pos_if_break;
    float penalty;
    bool is_hyphenated;
  };
  Vector<Candidate> candidates;
  candidates.push_back(Candidate{start, 0, 0, 0, false});
  const auto trimmed_end = [&](unsigned from, unsigned end) {
    while (end > from && (IsBidiTrailingSpace(text[end - 1]) || IsForcedBreak(text[end - 1]))) --end;
    return end;
  };
  for (unsigned previous = start; previous < limit;) {
    const unsigned opportunity = NextBreakOpportunity(*breaks_, previous + 1, limit);
    // Hyphenation opportunities of the word before this break opportunity.
    const StringView word_text(text, previous, opportunity - previous);
    unsigned word_start = previous;
    while (word_start < opportunity && LazyLineBreakIterator::IsBreakableSpace(text[word_start])) ++word_start;
    const unsigned word_end = trimmed_end(word_start, opportunity);
    if (word_start < word_end) {
      const ComputedStyle& style = StyleAtOffset(word_start);
      if (const Hyphenation* hyphenation = style.ShouldWrapLine() ? style.GetHyphenationWithLimits() : nullptr) {
        const Vector<wtf_size_t, 8> locations =
            hyphenation->HyphenLocations(StringView(text, word_start, word_end - word_start));
        const float hyphen_width = [&] {
          const String hyphen = style.HyphenString().GetString();
          return HarfBuzzShaper(hyphen).Shape(style.GetFont(), TextDirection::kLtr)->Width();
        }();
        for (wtf_size_t i = locations.size(); i-- > 0;) {
          const unsigned offset = word_start + locations[i];
          const float position = ContentWidth(start, offset);
          candidates.push_back(Candidate{offset, position, position + hyphen_width, hyphen_penalty, true});
        }
      }
    }
    if (opportunity >= limit) break;
    candidates.push_back(Candidate{opportunity, ContentWidth(start, opportunity),
                          ContentWidth(start, trimmed_end(start, opportunity)), 0, false});
    previous = opportunity;
  }
  const float paragraph_width = ContentWidth(start, trimmed_end(start, limit));
  candidates.push_back(Candidate{limit, paragraph_width, paragraph_width, 0, false});

  // Optimization not needed if one or no break opportunities in the paragraph.
  // The `candidates` has sentinels, one at the front and one at the back, so
  // `2` means no break opportunities, `3` means one.
  constexpr wtf_size_t kMinCandidates = 3;
  if (candidates.size() < kMinCandidates + 2) return false;

  if (candidates.size() >= 4) {
    // Increase penalties to minimize typographic orphans.
    constexpr float kOrphansPenalty = 10000;
    const float orphans_penalty = kOrphansPenalty * zoom;
    for (wtf_size_t i = candidates.size() - 1; i-- > 0;) {
      candidates[i].penalty += orphans_penalty;
      if (!candidates[i].is_hyphenated) break;
    }
  }

  // ComputeLineWidths(): only the first line of the block has an indent.
  const LayoutUnit first_line_indent = TextIndent(start);
  const auto available_width_to_fit = [&](wtf_size_t line_index) {
    LayoutUnit width = options_.available_inline_size;
    if (line_index == 0) width -= first_line_indent;
    return width.ClampNegativeToZero().AddEpsilon(); // Match `LineBreaker`.
  };

  // ComputeScores().
  struct Score {
    float score;
    wtf_size_t prev_index;
    wtf_size_t line_index;
  };
  constexpr float kScoreInfinity = std::numeric_limits<float>::max();
  constexpr float kScoreOverfull = 1e12f;
  constexpr float kLastLinePenaltyMultiplier = 4.0f;
  Vector<Score> scores;
  scores.push_back(Score{0, 0, 0});
  wtf_size_t active = 0;
  for (wtf_size_t end = 1; end < candidates.size(); ++end) {
    const Candidate& end_candidate = candidates[end];
    const bool is_end_last_candidate = end == candidates.size() - 1;
    float best = kScoreInfinity;
    wtf_size_t best_prev_index = 0;

    wtf_size_t last_line_index = scores[active].line_index;
    LayoutUnit width_to_fit = available_width_to_fit(last_line_index);
    float start_edge = end_candidate.pos_if_break - width_to_fit.ToFloat();
    float best_hope = 0;

    for (wtf_size_t start_index = active; start_index < end; ++start_index) {
      const Score& start_score = scores[start_index];
      const wtf_size_t line_index = start_score.line_index;
      if (line_index != last_line_index) {
        last_line_index = line_index;
        const LayoutUnit new_width_to_fit = available_width_to_fit(line_index);
        if (new_width_to_fit != width_to_fit) {
          width_to_fit = new_width_to_fit;
          start_edge = end_candidate.pos_if_break - width_to_fit.ToFloat();
          best_hope = 0;
        }
      }
      const float start_score_value = start_score.score;
      if (start_score_value + best_hope >= best) continue;
      const Candidate& start_candidate = candidates[start_index];
      const float delta = start_candidate.pos_no_break - start_edge;

      float width_score = 0;
      float additional_penalty = 0;
      if ((is_end_last_candidate || !is_justified) && delta < 0) {
        width_score = kScoreOverfull;
      } else if (is_end_last_candidate && !is_balanced) {
        // Increase penalty for hyphen on last line.
        additional_penalty = kLastLinePenaltyMultiplier * start_candidate.penalty;
      } else if (delta < 0) {
        width_score = kScoreOverfull;
      } else {
        // Penalties/scores should be a zoomed value. Because `delta` is zoomed,
        // unzoom once.
        width_score = delta * delta / zoom;
      }
      if (delta < 0) active = start_index + 1;
      else best_hope = width_score;
      const float score = start_score_value + width_score + additional_penalty;
      if (score <= best) {
        best = score;
        best_prev_index = start_index;
      }
    }
    scores.push_back(Score{best + end_candidate.penalty + line_penalty, best_prev_index,
                      scores[best_prev_index].line_index + 1});
  }

  // ComputeBreakPoints().
  Vector<BreakPoint> break_points;
  for (wtf_size_t i = scores.size() - 1, prev_index; i > 0; i = prev_index) {
    prev_index = scores[i].prev_index;
    break_points.push_back(BreakPoint{candidates[i].offset, candidates[i].is_hyphenated});
  }
  break_points.Reverse();
  if (break_points.empty() || break_points.size() > max_lines) return false;
  break_points_ = std::move(break_points);
  break_point_index_ = 0;
  return true;
}

// ParagraphLineBreaker::AttemptParagraphBalancing(): the narrowest available
// width that keeps the number of greedy lines, by bisection.
std::optional<LayoutUnit> InlineLayoutAlgorithm::AttemptParagraphBalancing() {
  constexpr wtf_size_t kMaxLinesForBalance = 6;
  const String& text = result_->TextContent();
  // Bisecting can't find the desired value if the paragraph has forced line
  // breaks.
  for (unsigned i = 0; i < text.length(); ++i)
    if (IsForcedBreak(text[i])) return std::nullopt;
  const LayoutUnit available_width = options_.available_inline_size;
  if (available_width <= LayoutUnit()) return std::nullopt;
  const ComputedStyle& block_style = context_.root_->Style();

  // Estimate the number of lines to see if the text is too long to balance.
  // Because this is an estimate, allow it to be `max_lines * 2`.
  if (const SimpleFontData* font = block_style.GetFont()->PrimaryFont()) {
    const float space_width = font->SpaceWidth();
    if (space_width > 0) {
      const wtf_size_t num_line_chars = static_cast<wtf_size_t>(available_width.ToFloat() / space_width);
      // The width is too narrow, don't balance.
      if (num_line_chars <= 0) return std::nullopt;
      const wtf_size_t estimated_num_lines = (text.length() + num_line_chars - 1) / num_line_chars;
      if (estimated_num_lines > kMaxLinesForBalance * 2) return std::nullopt;
    }
  }

  // LineBreakResults::BreakLines(): the number of lines, or nullopt when it
  // exceeds `max_lines`; `width_sum` receives the sum of the line widths.
  const auto break_lines = [&](LayoutUnit width, wtf_size_t max_lines, LayoutUnit* width_sum) -> std::optional<wtf_size_t> {
    wtf_size_t num_lines = 0;
    for (unsigned line_start = 0; line_start < text.length();) {
      if (num_lines >= max_lines) return std::nullopt;
      const LineBreakResult result = BreakLine(line_start, width);
      if (width_sum) *width_sum += Measure(line_start, result.end, nullptr, result.is_hyphenated);
      ++num_lines;
      if (result.end <= line_start) return std::nullopt;
      line_start = result.end;
    }
    return num_lines;
  };
  LayoutUnit width_sum;
  const std::optional<wtf_size_t> num_lines = break_lines(available_width, kMaxLinesForBalance, &width_sum);
  // Balancing not needed for single line paragraphs.
  if (!num_lines || *num_lines <= 1) return std::nullopt;

  // The bisect less than 1 pixel is worthless, so ignore. Use CSS pixels
  // instead of device pixels to make the algorithm consistent across different
  // zoom levels, but make sure it's not zero to avoid infinite loop.
  const LayoutUnit epsilon = LayoutUnit::FromFloatCeil(block_style.EffectiveZoom());
  // Start the bisect with the minimum value at the average line width, with
  // 20% buffer for potential edge cases.
  const LayoutUnit avg_line_width = width_sum / static_cast<int>(*num_lines);
  LayoutUnit upper = available_width;
  LayoutUnit lower = LayoutUnit::FromFloatRound(avg_line_width * .8f);
  while (lower + epsilon < upper) {
    const LayoutUnit middle = (upper + lower) / 2;
    if (break_lines(middle, *num_lines, nullptr)) upper = middle;
    else lower = middle;
  }
  return upper;
}

// The style of the text at `offset`, or of the block.
const ComputedStyle& InlineLayoutAlgorithm::StyleAtOffset(unsigned offset) const {
  for (const auto& item : result_->inline_items_) {
    if (item.type == InlineItem::kText && item.start <= offset && offset < item.end) return item.object->Style();
    if (item.start > offset) break;
  }
  return context_.root_->Style();
}

LayoutUnit InlineLayoutAlgorithm::Measure(unsigned start, unsigned end, bool* has_content, bool hyphenated) {
  const auto items = ShapeLine(start, end, hyphenated);
  if (has_content) *has_content = !items.empty();
  // LineInfo::ComputeWidth() includes 'text-indent'.
  LayoutUnit width = TextIndent(start);
  for (const auto& item : items) width += item.inline_size_;
  const String& text = result_->TextContent();
  const bool is_last_line = end == text.length() || IsForcedBreak(text[end]);
  return width - HangingTrailingSpaceWidth(items, width, is_last_line);
}

// LineBreaker::PrepareNextLine(): 'text-indent' applies to the first formatted
// line of the block.
LayoutUnit InlineLayoutAlgorithm::TextIndent(unsigned line_start) const {
  if (line_start) return LayoutUnit();
  return MinimumValueForLength(context_.root_->Style().TextIndent(), options_.available_inline_size);
}

// ShapingLineBreaker::Hyphenate(): the last hyphenation opportunity of the
// overflowing word [word_start, word_end) for which the line fits, or the
// first one when the word starts the line. 0 if none.
unsigned InlineLayoutAlgorithm::Hyphenate(unsigned line_start, unsigned word_start, unsigned word_end) {
  const String& text = result_->TextContent();
  while (word_start < word_end && LazyLineBreakIterator::IsBreakableSpace(text[word_start])) ++word_start;
  while (word_end > word_start && LazyLineBreakIterator::IsBreakableSpace(text[word_end - 1])) --word_end;
  if (word_start >= word_end) return 0;
  // The word must be in one text item: hyphenation follows its style.
  const InlineItem* word_item = nullptr;
  for (const auto& item : result_->inline_items_) {
    if (item.type == InlineItem::kText && item.start <= word_start && word_start < item.end) {
      word_item = &item;
      break;
    }
  }
  if (!word_item || word_end > word_item->end) return 0;
  const ComputedStyle& style = word_item->object->Style();
  if (!style.ShouldWrapLine() || style.WordBreak() == EWordBreak::kAutoPhrase) return 0;
  const Hyphenation* hyphenation = style.GetHyphenationWithLimits();
  if (!hyphenation) return 0;
  const StringView word(text, word_start, word_end - word_start);
  const Vector<wtf_size_t, 8> locations = hyphenation->HyphenLocations(word);
  // |locations| is a list of hyphenation points in the descending order.
  for (const wtf_size_t location : locations) {
    const unsigned end = word_start + location;
    if (Measure(line_start, end, nullptr, true) <= available_width_) return end;
  }
  if (line_start == word_start && !locations.empty()) return word_start + locations.back();
  return 0;
}
// LineBreaker::SetCurrentStyle(), HandleOpenTag(), HandleCloseTag() and
// CanBreakAfterAtomicInline(). Container boundaries can add opportunities,
// even when the adjacent text itself has nowrap.
unsigned InlineLayoutAlgorithm::NextBreakOpportunity(LazyLineBreakIterator& breaks, unsigned offset, unsigned limit,
                                                     bool break_anywhere) const {
  const String& text = result_->TextContent();
  const unsigned length = text.length();
  const auto& items = result_->inline_items_;
  const ComputedStyle* current_style = &context_.root_->Style();
  const auto set_style = [&](const ComputedStyle& style) {
    current_style = &style;
    if (style.ShouldWrapLine()) ConfigureBreakIterator(breaks, style, break_anywhere);
  };
  const auto is_space = [](UChar c) { return c == uchar::kSpace || c == uchar::kTab; };
  // Empty results transfer the preceding opportunity. Open tags start a new
  // result without transferring it; close tags propagate it unconditionally.
  const auto advance_past_empty = [&](wtf_size_t i, unsigned candidate) {
    for (++i; i < items.size() && candidate < limit; ++i) {
      const auto& following = items[i];
      if (following.type == InlineItem::kOpenTag) break;
      if (following.start == following.end) continue;
      if (!IsNonFragmentItem(following, text)) break;
      candidate = std::min(following.end, limit);
    }
    return candidate;
  };
  set_style(*current_style);
  for (wtf_size_t i = 0; i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.start >= limit) break;
    if (item.type == InlineItem::kOpenTag || item.type == InlineItem::kCloseTag) {
      const bool was_auto_wrap = current_style->ShouldWrapLine();
      set_style(item.type == InlineItem::kOpenTag ? item.object->Style() : item.object->Parent()->Style());
      if (item.end < offset || !i) continue;
      bool can_break = false;
      if (item.type == InlineItem::kOpenTag) {
        if (!was_auto_wrap && current_style->ShouldWrapLine() && items[i - 1].type == InlineItem::kText)
          can_break = breaks.IsBreakable(item.end);
      } else if (was_auto_wrap) {
        const bool preceded_by_space = item.end && is_space(text[item.end - 1]);
        const bool only_after_space = current_style->ShouldPreserveWhiteSpaces() && current_style->ShouldWrapLine();
        can_break = is_space(text[item.end]) && (!only_after_space || preceded_by_space);
      } else if (current_style->ShouldWrapLine() && item.end && !is_space(text[item.end - 1])) {
        can_break = breaks.IsBreakable(item.end);
      }
      if (can_break) return advance_past_empty(i, item.end);
      continue;
    }
    if (item.start == item.end) continue;
    if (IsNonFragmentItem(item, text)) continue;
    // An atomic inline's own wrapping mode controls its contents, not breaks
    // around it in the parent's inline formatting context.
    set_style(item.type == InlineItem::kAtomicInline ? item.object->Parent()->Style() : item.object->Style());
    if (item.end < offset) continue;
    if (item.is_generated_for_line_break) return advance_past_empty(i, item.end);
    if (!current_style->ShouldWrapLine()) continue;
    if (item.type == InlineItem::kAtomicInline) return advance_past_empty(i, item.end);
    const unsigned item_end = std::min(item.end, limit);
    // Search one character beyond the item: reaching the search limit alone
    // must not introduce a break inside a word split across text objects.
    unsigned candidate = breaks.NextBreakOpportunity(std::max(offset, item.start + 1),
                                                      item_end < length ? item_end + 1 : length);
    if (candidate > item_end && item_end == item.end && item_end < length &&
        item.type == InlineItem::kText && text[item_end] == uchar::kObjectReplacementCharacter) {
      // Atomic boxes have a break before them even next to NBSP. Do not apply
      // this to a literal U+FFFC in a text object.
      for (wtf_size_t next = i + 1; next < items.size(); ++next) {
        if (items[next].start == items[next].end) continue;
        if (items[next].type == InlineItem::kAtomicInline) candidate = item_end;
        break;
      }
    }
    if (candidate <= item_end) {
      return candidate == item.end ? advance_past_empty(i, candidate) : candidate;
    }
    if (item_end == limit) return limit;
  }
  return limit;
}

// LineOffsetForTextAlign() in length_utils.cc.
static LayoutUnit LineOffsetForTextAlign(ETextAlign text_align, TextDirection direction, LayoutUnit space_left) {
  bool is_ltr = IsLtr(direction);
  if (text_align == ETextAlign::kStart || text_align == ETextAlign::kJustify)
    text_align = is_ltr ? ETextAlign::kLeft : ETextAlign::kRight;
  else if (text_align == ETextAlign::kEnd)
    text_align = is_ltr ? ETextAlign::kRight : ETextAlign::kLeft;

  switch (text_align) {
    case ETextAlign::kLeft:
    case ETextAlign::kWebkitLeft: {
      // The direction of the block should determine what happens with wide
      // lines. In particular with RTL blocks, wide lines should still spill
      // out to the left.
      if (is_ltr) return LayoutUnit();
      return space_left.ClampPositiveToZero();
    }
    case ETextAlign::kRight:
    case ETextAlign::kWebkitRight: {
      // In RTL, trailing spaces appear on the left of the line.
      if (!is_ltr) [[unlikely]] {
        return space_left;
      }
      // Wide lines spill out of the block based off direction.
      // So even if text-align is right, if direction is LTR, wide lines
      // should overflow out of the right side of the block.
      if (space_left > LayoutUnit()) return space_left;
      return LayoutUnit();
    }
    case ETextAlign::kCenter:
    case ETextAlign::kWebkitCenter: {
      if (is_ltr) return (space_left / 2).ClampNegativeToZero();
      // In RTL, trailing spaces appear on the left of the line.
      if (space_left > LayoutUnit()) return (space_left / 2).ClampNegativeToZero();
      // In RTL, wide lines should spill out to the left, same as kRight.
      return space_left;
    }
    default: NOTREACHED();
  }
}

// ApplyJustificationInternal() in justification_utils.cc: expands the
// opportunities of [line_text_start, end_offset) by `space`. False when the
// line cannot be justified.
bool InlineLayoutAlgorithm::ApplyJustification(LayoutUnit space, unsigned end_offset, HeapVector<FragmentItem>& items) {
  // If this line overflows, fallback to 'text-align: start'.
  if (space <= 0) return false;
  const FragmentItem* first = nullptr;
  for (const auto& item : items) {
    if (!item.IsGeneratedText()) {
      first = &item;
      break;
    }
  }
  if (!first) return false;
  const unsigned line_text_start_offset = first->text_offset_.start;
  // Can't justify an empty string.
  if (end_offset <= line_text_start_offset) return false;

  // BuildJustificationText(): the line text, with the hyphen of a hyphenated
  // line, and without a trailing forced break.
  const String& text_content = result_->TextContent();
  StringBuilder line_text_builder;
  line_text_builder.Append(StringView(text_content, line_text_start_offset, end_offset - line_text_start_offset));
  if (!items.empty() && items.back().IsGeneratedText()) {
    line_text_builder.Append(items.back().generated_text_);
  } else {
    wtf_size_t text_length = line_text_builder.length();
    if (text_length > 0u && line_text_builder[text_length - 1] == uchar::kLineFeed) {
      if (text_length == 1u) return false;
      line_text_builder.Resize(text_length - 1);
    }
  }
  const String line_text = line_text_builder.ToString();
  if (line_text.empty()) return false;

  // Compute the spacing to justify.
  ShapeResultSpacing<String> spacing(line_text);
  spacing.SetExpansion(space, base_direction_);
  if (!spacing.HasExpansion()) return false;

  // JustifyResults().
  for (FragmentItem& item : items) {
    if (item.IsGeneratedText()) continue;
    if (item.text_offset_.start >= end_offset) break;
    if (item.shape_ && !item.IsLineBreak()) {
      std::shared_ptr<ShapeResult> shape_result = item.shape_->CreateShapeResult();
      shape_result->ApplySpacing(spacing, static_cast<int>(item.text_offset_.start - line_text_start_offset) -
                                              static_cast<int>(shape_result->StartIndex()));
      item.inline_size_ = shape_result->SnappedWidth();
      item.shape_ = ShapeResultView::Create(shape_result.get());
    } else if (item.IsAtomicInline()) {
      float spacing_before = 0.0f;
      const unsigned line_text_offset = item.text_offset_.start - line_text_start_offset;
      const float spacing_after = spacing.ComputeSpacing(line_text_offset, spacing_before);
      item.inline_size_ += LayoutUnit(spacing_after);
    }
  }
  return true;
}

void InlineLayoutAlgorithm::PlaceLine(unsigned start, unsigned end, bool soft_wrap, bool hyphenated) {
  HeapVector<FragmentItem> items = ShapeLine(start, end, hyphenated);
  const String& text = result_->TextContent();
  const auto& root_style = context_.root_->LayoutStyle();
  const FontBaseline baseline_type = root_style->GetFontBaseline();

  // LineInfo: width (including 'text-indent'), hanging trailing spaces and
  // the text-align of the line.
  const LayoutUnit text_indent = TextIndent(start);
  LayoutUnit width = text_indent;
  for (const FragmentItem& item : items) width += item.inline_size_;
  const bool is_last_line = !soft_wrap;
  const LayoutUnit hang_width = HangingTrailingSpaceWidth(items, width, is_last_line);
  ETextAlign text_align = root_style->GetTextAlign(is_last_line);

  // InlineLayoutAlgorithm::ApplyTextAlign(): justification changes item
  // sizes before they are placed.
  LayoutUnit space = available_width_ - (width - hang_width);
  if (text_align == ETextAlign::kJustify) {
    // Justify the end of visible text, ignoring preserved trailing spaces.
    unsigned end_offset_for_justify = end;
    while (end_offset_for_justify > start &&
           (IsBidiTrailingSpace(text[end_offset_for_justify - 1]) || IsForcedBreak(text[end_offset_for_justify - 1])))
      --end_offset_for_justify;
    if (!items.empty() && ApplyJustification(space, end_offset_for_justify, items)) {
      width = text_indent;
      for (const FragmentItem& item : items) width += item.inline_size_;
      space = LayoutUnit();
    } else {
      // If justification fails, fallback to 'text-align: start'.
      text_align = ETextAlign::kStart;
    }
  }
  const LayoutUnit line_offset_for_text_align = LineOffsetForTextAlign(text_align, base_direction_, space);

  // LogicalLineBuilder::PlaceItems(): walk the inline items of the line in
  // logical order with a box state for each inline box.
  const auto& units = result_->inline_items_;
  wtf_size_t unit_index = 0;
  Vector<const InlineObject*> continuing_boxes;
  for (; unit_index < units.size(); ++unit_index) {
    const InlineItem& unit = units[unit_index];
    if (unit.start > start || (unit.start == start && unit.type != InlineItem::kCloseTag)) break;
    if (unit.type == InlineItem::kOpenTag) continuing_boxes.push_back(unit.object);
    else if (unit.type == InlineItem::kCloseTag && !continuing_boxes.empty()) continuing_boxes.pop_back();
  }
  InlineLayoutStateStack box_states;
  HeapVector<FragmentItem> placed;
  placed.ReserveInitialCapacity(items.size());
  InlineBoxState* box = box_states.OnBeginPlaceItems(*context_.root_, continuing_boxes, baseline_type, placed);
  wtf_size_t next_fragment = 0;
  const auto place_text = [&](FragmentItem&& item) {
    // Take all used fonts into account if 'line-height: normal'.
    if (box->include_used_fonts && item.shape_) box->AccumulateUsedFonts(item.shape_.get());
    item.block_offset_ = box->text_top - item.Style().LegacyBaselineShift();
    item.block_size_ = box->text_height;
    box_states.CapturePaintOffsets(item, baseline_type);
    if (const LayoutUnit shift = item.Style().LegacyBaselineShift()) {
      // Compatibility input: the legacy shift moves the text and its strut.
      FontHeight shifted = box->text_metrics;
      shifted.Move(-shift);
      box->metrics.Unite(shifted);
    }
    placed.push_back(std::move(item));
  };
  for (; unit_index < units.size(); ++unit_index) {
    const InlineItem& unit = units[unit_index];
    if (unit.start > end || (unit.start == end && unit.type != InlineItem::kCloseTag && unit.start != unit.end))
      break;
    if (unit.start == end && unit.type == InlineItem::kOpenTag) break;
    switch (unit.type) {
      case InlineItem::kOpenTag:
        box = box_states.OnOpenTag(*unit.object, true, baseline_type, placed);
        break;
      case InlineItem::kCloseTag: box = box_states.OnCloseTag(&placed, box, baseline_type); break;
      case InlineItem::kAtomicInline:
        while (next_fragment < items.size() && items[next_fragment].object_ == unit.object &&
               items[next_fragment].text_offset_.start < unit.end) {
          FragmentItem item = std::move(items[next_fragment++]);
          // LogicalLineBuilder::PlaceAtomicInline(): a box of its own, without
          // a strut, whose metrics are its baseline metrics.
          box = box_states.OnOpenTag(*item.object_, false, baseline_type, placed);
          if (item.text_combine_) [[unlikely]] {
            // The metrics should be as text instead of atomic inline box.
            box->is_text_combine = true;
            box->ComputeTextMetrics(*item.style_, *item.style_->GetFont(), baseline_type);
            item.block_size_ = item.text_combine_->Size().width;
            item.block_offset_ = box->text_top;
          } else {
            const FontHeight metrics(item.object_->LayoutAtomicBaseline(),
                                     item.object_->LayoutAtomicSize().block_size - item.object_->LayoutAtomicBaseline());
            box->metrics.Unite(metrics);
            item.block_size_ = item.object_->LayoutAtomicSize().block_size;
            item.block_offset_ = -metrics.ascent;
          }
          placed.push_back(std::move(item));
          box = box_states.OnCloseTag(&placed, box, baseline_type);
        }
        break;
      default:
        while (next_fragment < items.size() && items[next_fragment].object_ == unit.object &&
               !items[next_fragment].IsGeneratedText() && items[next_fragment].text_offset_.start >= unit.start &&
               items[next_fragment].text_offset_.start < unit.end) {
          place_text(std::move(items[next_fragment++]));
          // LogicalLineBuilder::PlaceHyphen(): the hyphen follows its text.
          if (next_fragment < items.size() && items[next_fragment].IsGeneratedText() &&
              items[next_fragment].object_ == unit.object)
            place_text(std::move(items[next_fragment++]));
        }
        break;
    }
  }
  // Items not reached by the walk (none expected) are placed in the current
  // box.
  while (next_fragment < items.size()) place_text(std::move(items[next_fragment++]));
  box_states.OnEndPlaceItems(&placed, baseline_type);
  items = std::move(placed);
  const FontHeight metrics = box_states.LineBoxState().metrics.IsEmpty() ? FontHeight()
                                                                         : box_states.LineBoxState().metrics;

  // LogicalLineBuilder::BidiReorder: use resolved levels for runs and the base
  // direction for trailing whitespace items. There are no opaque block items.
  Vector<UBiDiLevel, 32> levels;
  levels.ReserveInitialCapacity(items.size());
  for (const auto& item : items) levels.push_back(static_cast<UBiDiLevel>(item.bidi_level_));
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
    levels[i - 1] = IsLtr(base_direction_) ? 0 : 1;
  }
  Vector<int32_t, 32> visual(items.size());
  if (!items.empty()) BidiParagraph::IndicesInVisualOrder(levels, visual);

  // InlineLayoutAlgorithm::CreateLine(): text-align moves the line box as a
  // whole; 'text-indent' shifts it at the line-left in LTR. Hanging spaces
  // start before the line offset in RTL (AdjustLineOffsetForHanging()).
  LayoutUnit line_offset = line_offset_for_text_align;
  if (IsLtr(base_direction_)) line_offset += text_indent;
  const LayoutUnit position = IsLtr(base_direction_) ? LayoutUnit() : -hang_width;

  FragmentItem line;
  line.type_ = FragmentItem::kLine;
  line.style_ = root_style;
  line.text_offset_ = {start, end};
  line.bidi_level_ = IsLtr(base_direction_) ? 0 : 1;
  line.block_offset_ = block_offset_;
  line.block_size_ = metrics.LineHeight();
  line.baseline_ = metrics.ascent;
  line.descendants_count_ = items.size() + 1;
  line.soft_wrap_ = soft_wrap;
  line.inline_offset_ = line_offset;
  const size_t line_index = result_->items_.size();
  result_->items_.push_back(std::move(line));
  LayoutUnit inline_offset = line_offset + position;
  for (int32_t index : visual) {
    FragmentItem& item = items[index];
    item.inline_offset_ = inline_offset;
    item.block_offset_ += block_offset_ + metrics.ascent;
    for (auto& paint_box : item.paint_boxes_) paint_box.block_offset += block_offset_ + metrics.ascent;
    item.line_index_ = line_index;
    inline_offset += item.inline_size_;
    result_->items_.push_back(std::move(item));
  }
  // The line box size excludes the hanging width.
  result_->items_[line_index].inline_size_ = (inline_offset - line_offset - position - hang_width).ClampNegativeToZero();
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
  const WritingMode writing_mode = result_->writing_mode_;
  const bool horizontal = IsHorizontalWritingMode(writing_mode);
  result_->physical_size_ = horizontal ? PhysicalSize(options_.available_inline_size, block_offset_) : PhysicalSize(block_offset_, options_.available_inline_size);
  const FragmentItem* line = nullptr;
  for (auto& item : result_->items_) {
    if (item.Type() == FragmentItem::kLine) line = &item;
    if (horizontal) {
      item.rect_ = {item.inline_offset_, item.block_offset_, item.inline_size_, item.block_size_};
    } else {
      LayoutUnit left = IsFlippedBlocksWritingMode(writing_mode) ? block_offset_ - item.block_offset_ - item.block_size_ : item.block_offset_;
      // FragmentItemsBuilder uses ToLineWritingMode(): vertical-lr lines
      // progress rightwards, but their contents still have line-over on right.
      if (IsFlippedLinesWritingMode(writing_mode) && item.Type() != FragmentItem::kLine)
        left = line->block_offset_ + line->block_size_ -
               (item.block_offset_ - line->block_offset_) - item.block_size_;
      const LayoutUnit top = writing_mode == WritingMode::kSidewaysLr ? options_.available_inline_size - item.inline_offset_ - item.inline_size_ : item.inline_offset_;
      item.rect_ = {left, top, item.block_size_, item.inline_size_};
    }
  }
}

std::unique_ptr<FragmentItems> InlineLayoutAlgorithm::Layout() {
  if (context_.needs_collect_inlines_ || !context_.fragments_) {
    InlineItemsBuilder builder(&result_->inline_items_, &mapping_builder_, *context_.root_);
    builder.EnterBlock(context_.root_->Style());
    Collect(*context_.root_, builder);
    builder.ExitBlock();
    String text = builder.ToString();
    // InlineItemsBuilder::DidFinishCollectInlines(): retain these flags before
    // normalizing the local layout buffer to UTF-16.
    const bool is_bidi_enabled = builder.HasBidiControls() ||
                                (builder.HasNonOrc16BitCharacters() && Character::MaybeBidiRtl(text));
    // Upstream's bidi analysis itself widens the buffer before script
    // segmentation, even if it subsequently disables bidi for all-LTR text.
    const bool use_latin1_script = (!is_bidi_enabled && text.Is8Bit()) || !builder.HasNonOrc16BitCharacters();
    text.Ensure16Bit();
    if (!mapping_builder_.SetDestinationString(text)) NOTREACHED();
    result_->mapping_ = mapping_builder_.Build(*context_.root_);
    for (const auto& item : result_->inline_items_) styles_.push_back(item.object->LayoutStyle());
    SegmentAndShape(use_latin1_script, is_bidi_enabled);
    ApplyTextAutoSpace();
  } else {
    ReuseCollectedItems(*context_.fragments_);
  }
  const String& text = result_->TextContent();
  const ComputedStyle& block_style = context_.root_->Style();
  result_->writing_mode_ = block_style.GetWritingMode();
  result_->direction_ = block_style.Direction();
  const bool plaintext = block_style.GetUnicodeBidi() == UnicodeBidi::kPlaintext;
  unsigned start = ReuseLines();
  const auto* locale = block_style.GetFont()->GetFontDescription().Locale();
  breaks_.emplace(text, locale);
  emergency_breaks_.emplace(text, locale);
  base_direction_ = block_style.Direction();
  const auto update_base_direction = [&](unsigned line_start) {
    // 'unicode-bidi: plaintext': each paragraph takes its direction from its
    // first strong character (BidiParagraph::BaseDirectionForStringOrLtr).
    if (!plaintext) return;
    unsigned paragraph_start = line_start;
    while (paragraph_start && !IsForcedBreak(text[paragraph_start - 1])) --paragraph_start;
    base_direction_ = BidiParagraph::BaseDirectionForStringOrLtr(StringView(text, paragraph_start), IsLineFeed);
  };

  // LineBreakStrategy: 'text-wrap-style: balance' balances the first
  // paragraph of the block (ScoreLineBreaker), or the whole block by bisecting
  // the available width; 'pretty' optimizes the last lines of each paragraph.
  const TextWrapStyle text_wrap = block_style.GetTextWrapStyle();
  available_width_ = options_.available_inline_size;
  if (text_wrap == TextWrapStyle::kBalance && !start && block_style.ShouldWrapLine()) {
    if (!ComputeScoreBreakPoints(start, /*is_balanced=*/true)) {
      if (const std::optional<LayoutUnit> balanced = AttemptParagraphBalancing()) available_width_ = *balanced;
    }
  }

  while (start < text.length()) {
    update_base_direction(start);
    if (break_point_index_ >= break_points_.size()) {
      break_points_.clear();
      break_point_index_ = 0;
      if (text_wrap == TextWrapStyle::kPretty && block_style.ShouldWrapLine())
        (void)ComputeScoreBreakPoints(start, /*is_balanced=*/false);
    }
    unsigned end;
    bool hyphenated;
    LayoutUnit line_available_width = available_width_;
    if (break_point_index_ < break_points_.size()) {
      // LineBreaker::SetBreakAt(): the break point of the score line breaker.
      // The line keeps the original available width for alignment.
      end = break_points_[break_point_index_].offset;
      hyphenated = break_points_[break_point_index_].is_hyphenated;
      ++break_point_index_;
      line_available_width = options_.available_inline_size;
    } else {
      const LineBreakResult result = BreakLine(start, available_width_);
      end = result.end;
      hyphenated = result.is_hyphenated;
    }
    unsigned limit = start;
    while (limit < text.length() && !IsForcedBreak(text[limit])) ++limit;
    const bool forced = end == limit && limit < text.length();
    if (forced) ++end;
    assert(end > start);
    const LayoutUnit saved_available_width = available_width_;
    available_width_ = line_available_width;
    PlaceLine(start, end, !forced && end < text.length(), hyphenated);
    available_width_ = saved_available_width;
    start = end;
  }
  if (text.empty() || IsForcedBreak(text[text.length() - 1])) {
    update_base_direction(start);
    PlaceLine(start, start, false);
  }
  ToPhysicalCoordinates();
  result_->FinalizeAfterLayout();
  result_->runs_ = std::move(runs_);
  result_->levels_ = std::move(levels_);
  return std::move(result_);
}

} // namespace bkit
