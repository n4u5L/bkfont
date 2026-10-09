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
// A soft hyphen generates a visible hyphen only at a wrapped line end.
bool IsSoftHyphenBreak(const String& text, unsigned start, unsigned end) {
  return end > start && end < text.length() && text[end - 1] == uchar::kSoftHyphen && !IsForcedBreak(text[end]);
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

// SetCurrentStyleForce() disables hyphenation for auto-phrase, unless
// line-break: anywhere takes precedence over word-break.
const Hyphenation* HyphenationForStyle(const ComputedStyle& style) {
  if (!style.ShouldWrapLine() || LineBreakTypeFor(style) == LineBreakType::kPhrase) return nullptr;
  return style.GetHyphenationWithLimits();
}

// LineBreaker::break_anywhere_if_overflow_ in LineBreakerMode::kContent.
bool BreakAnywhereIfOverflow(const ComputedStyle& style) {
  if (!style.ShouldWrapLine() || style.GetLineBreak() == LineBreak::kAnywhere) return false;
  return style.WordBreak() == EWordBreak::kBreakWord || style.OverflowWrap() != EOverflowWrap::kNormal;
}

// LineBreaker::SetCurrentStyleForce(): configures `breaks` for `style`.
// `break_anywhere` is override_break_anywhere_.
void ConfigureBreakIterator(LazyLineBreakIterator& breaks, const ComputedStyle& style, bool break_anywhere,
                             bool disable_phrase = false) {
  breaks.SetLocale(style.GetFontDescription().Locale());
  Hyphens hyphens = style.GetHyphens();
  if (style.GetLineBreak() == LineBreak::kAnywhere) {
    breaks.SetStrictness(LineBreakStrictness::kDefault);
    breaks.SetBreakType(LineBreakType::kBreakCharacter);
  } else {
    breaks.SetStrictness(StrictnessFromLineBreak(style.GetLineBreak()));
    LineBreakType type = LineBreakTypeFor(style);
    if (type == LineBreakType::kPhrase) {
      if (disable_phrase) type = LineBreakType::kNormal;
      else hyphens = Hyphens::kNone;
    }
    if (break_anywhere && BreakAnywhereIfOverflow(style)) type = LineBreakType::kBreakCharacter;
    breaks.SetBreakType(type);
  }
  breaks.EnableSoftHyphen(hyphens != Hyphens::kNone);
  breaks.SetBreakSpace(style.ShouldBreakSpaces() ? BreakSpaceType::kAfterEverySpace : BreakSpaceType::kAfterSpaceRun);
}

using RunSegmenterRange = RunSegmenter::RunSegmenterRange;

// Collected items and shaping runs are ordered by logical text offset. Skip
// the prefix in one lookup when measuring or shaping an arbitrary line range.
template <typename Items>
wtf_size_t FirstItemEndingAfter(const Items& items, unsigned offset) {
  return std::lower_bound(items.begin(), items.end(), offset,
                          [](const auto& item, unsigned offset) { return item.end <= offset; }) - items.begin();
}

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

bool SameSegmentData(const RunSegmenterRange& a, const RunSegmenterRange& b) {
  return a.script == b.script && a.render_orientation == b.render_orientation &&
         a.font_fallback_priority == b.font_fallback_priority;
}

// Compare the segmentation of a line as ranges, without expanding the data
// into a key for each code unit. Boundaries matter even when the data matches:
// distinct mixed-orientation items are shaped separately upstream.
bool SameSegmentation(std::span<const RunSegmenterRange> a, std::span<const RunSegmenterRange> b,
                      unsigned start, unsigned end) {
  a = SegmentsForRange(a, start, end);
  b = SegmentsForRange(b, start, end);
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!SameSegmentData(a[i], b[i]) || std::max(start, a[i].start) != std::max(start, b[i].start) ||
        std::min(end, a[i].end) != std::min(end, b[i].end)) {
      return false;
    }
  }
  return true;
}

// LineBreaker::ShapeText(): line edges and score candidates use the same
// script/orientation ranges and CSS spacing as the initial shape.
std::unique_ptr<const ShapeResult> ReshapeText(const String& text, const Font& font, TextDirection direction,
                                              unsigned start, unsigned end,
                                              std::span<const RunSegmenterRange> segments,
                                              ShapeOptions options = {}) {
  HarfBuzzShaper shaper(text);
  auto shape = shaper.Shape(&font, direction, start, end, SegmentsForRange(segments, start, end), options);
  ShapeResultSpacing<String> spacing(text);
  if (spacing.SetSpacing(font.GetFontDescription())) shape->ApplySpacing(spacing);
  return shape;
}

// Keep line-edge shaping in this adapter, shared by measurement and placement.
class LineShaper final : public ShapingLineBreaker {
public:
  LineShaper(const String& text, const Font& font, const ShapeResult& shape,
              const LazyLineBreakIterator& breaks, std::span<const RunSegmenterRange> segments,
              const Hyphenation* hyphenation = nullptr)
       : ShapingLineBreaker(&shape, &breaks, hyphenation, &font),
        text_(text),
        font_(font),
        direction_(shape.Direction()),
        segments_(segments) {
  }

private:
  std::unique_ptr<const ShapeResult> Shape(unsigned start, unsigned end, ShapeOptions options) override {
    return ReshapeText(text_, font_, direction_, start, end, segments_, options);
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

static void AppendTransformedTextContent(const InlineObject& object, StringBuilder& builder,
                                         UChar& previous_character) {
  if (object.IsText() && !object.Text().empty()) {
    // LayoutText::TransformAndSecureText() runs before LayoutTextCombine
    // collects text. Apply each text object's own style and locale, retaining
    // the last transformed character for text-transform: capitalize.
    const String transformed = object.Style().ApplyTextTransform(object.Text(), previous_character, nullptr);
    if (!transformed.empty()) {
      builder.Append(transformed);
      previous_character = transformed[transformed.length() - 1];
    }
  } else if (object.IsAtomicInline()) {
    previous_character = uchar::kSpace;
  }
  for (const auto& child : object.Children()) AppendTransformedTextContent(*child, builder, previous_character);
}

void InlineLayoutAlgorithm::Collect(const InlineObject& object, InlineItemsBuilder& builder) {
  if (object.Parent() && !object.IsAtomicInline() && HasTextCombine(object.Style()) &&
      (!object.Parent()->Parent() || !HasTextCombine(object.Parent()->Style()))) {
    StringBuilder text;
    // LayoutText::PreviousCharacter() stops at the containing
    // LayoutTextCombine block for the first text child.
    UChar previous_character = uchar::kSpace;
    AppendTransformedTextContent(object, text, previous_character);
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
    std::span<const RunSegmenterRange> segments;
    bool is_8bit_text;
  };

  ReusingTextShaper(const HarfBuzzShaper& shaper, const String& text, std::optional<Previous> previous)
      : shaper_(shaper), text_(text), previous_(previous) {
    if (!previous_) return;
    const String& old_text = previous_->text;
    const unsigned old_length = old_text.length(), new_length = text.length();
    const unsigned limit = std::min(old_length, new_length);
    while (prefix_ < limit && old_text[prefix_] == text[prefix_]) ++prefix_;
    while (suffix_ < limit - prefix_ && old_text[old_length - 1 - suffix_] == text[new_length - 1 - suffix_]) ++suffix_;
  }

  std::unique_ptr<ShapeResult> Shape(const InlineItemRun& run,
                                     std::span<const RunSegmenterRange> ranges) const {
    const Font* font = run.style->GetFont();
    const TextDirection direction = DirectionFromLevel(run.level);
    ShapeOptions options = run.shape_options;
    Vector<Span> spans;
    if (previous_) {
      // Partial reuse is enabled only for a single script/orientation range.
      assert(ranges.size() == 1);
      // The prefix keeps its offsets; the suffix moves by the length change.
      const unsigned new_length = text_.length();
      const int delta = static_cast<int>(new_length) - static_cast<int>(previous_->text.length());
      if (run.start < prefix_)
        CollectSpans(run, *font, run.start, std::min(run.end, prefix_), 0, ranges.front(), spans);
      if (run.end > new_length - suffix_)
        CollectSpans(run, *font, std::max(run.start, new_length - suffix_), run.end, delta, ranges.front(), spans);
    }
    if (spans.empty()) return shaper_.Shape(font, direction, run.start, run.end, ranges, options);

    std::unique_ptr<ShapeResult> result = ShapeResult::CreateEmpty(*spans.front().old_run->shape);
    unsigned offset = run.start;
    for (const Span& span : spans) {
      if (offset < span.start) {
        Append(*Reshape(font, direction, offset, span.start, ranges, options), result.get());
        options.han_kerning_start = false;
      }
      const ShapeResult& shape = *span.old_run->shape;
      if (result->NumCharacters() || span.old_start == span.start) {
        // CopyRangeInternal renumbers appended characters to follow the
        // target. A shifted suffix therefore needs no intermediate copies.
        shape.CopyRange(span.old_start, span.old_end, result.get());
      } else {
        // InlineNodeDataEditor::ShiftItem(), applied to the new subrange
        // directly. Its runs are independent of the previous layout.
        result = shape.SubRange(span.old_start, span.old_end, span.start);
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
                    const RunSegmenterRange& range, Vector<Span>& spans) const {
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
      // The run-level check above covers bidi. Compare script, orientation,
      // and fallback by segment instead of checking every unchanged character.
      const unsigned from = std::max(old_start, old_run.start);
      const unsigned to = std::min(old_end, old_run.end);
      for (const auto& old_segment : SegmentsForRange(previous_->segments, from, to)) {
        if (SameSegmentData(old_segment, range)) {
          AddSpan(run, old_run, std::max(from, old_segment.start), std::min(to, old_segment.end), delta, spans);
        }
      }
    }
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
    // InlineNodeDataEditor consults the old text's storage width. The local
    // text buffer is always UTF-16, so use the width retained at collection.
    const unsigned skip = (previous_->is_8bit_text ? 2 : 10) - 1;
    if (end <= start + skip) return start;
    shape.EnsurePositionData();
    return std::max(start, shape.CachedPreviousSafeToBreakOffset(end - skip));
  }

  std::unique_ptr<ShapeResult> Reshape(const Font* font, TextDirection direction, unsigned start, unsigned end,
                                       std::span<const RunSegmenterRange> ranges, ShapeOptions options) const {
    return shaper_.Shape(font, direction, start, end, SegmentsForRange(ranges, start, end), options);
  }

  // ReusingTextShaper::AppendShapeResult().
  static void Append(const ShapeResult& shape, ShapeResult* target) {
    shape.CopyRange(shape.StartIndex(), shape.EndIndex(), target);
  }

  const HarfBuzzShaper& shaper_;
  const String& text_;
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
  // SegmentScriptRuns() reuses its result when recollection leaves the text
  // unchanged. Mixed vertical orientation also depends on item boundaries;
  // reuse here only when both layouts have horizontal typography.
  if (context_.fragments_ && context_.root_->Style().IsHorizontalTypographicMode()) {
    const FragmentItems& old = *context_.fragments_;
    if (IsHorizontalTypographicMode(old.GetWritingMode()) && !old.segments_.empty() && text == old.TextContent()) {
      segments = old.segments_;
      return segments.size() > 1;
    }
  }
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
        item.object->Style().GetFontDescription().Orientation() != FontOrientation::kVerticalMixed) {
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
    const auto& style = unit.object->LayoutStyle();
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
          runs_.back().level == levels_[start] && *runs_.back().style->GetFont() == *style->GetFont()) {
        runs_.back().end = end;
      } else {
        runs_.push_back(Run{start, end, levels_[start], style, unit.object, nullptr, control});
      }
      start = end;
    }
  }
  const bool has_segmented_text = SegmentText(use_latin1_script && !is_bidi_enabled);
  HarfBuzzShaper shaper(text);
  std::optional<ReusingTextShaper::Previous> previous;
  if (!has_segmented_text && context_.reuse_shape_results_ && context_.fragments_) {
    const FragmentItems& old = *context_.fragments_;
    previous.emplace(ReusingTextShaper::Previous{old.TextContent(), {old.runs_.data(), old.runs_.size()},
                                                 {old.segments_.data(), old.segments_.size()}, old.is_8bit_text_});
  }
  const ReusingTextShaper reusing_shaper(shaper, text, previous);
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
    run.shape = reusing_shaper.Shape(run, ranges);
    ShapeResultSpacing<String> spacing(text);
    if (spacing.SetSpacing(run.style->GetFont()->GetFontDescription())) run.shape->ApplySpacing(spacing);
  }
}

// InlineNode::ShapeText(): shape through compatible items, then distribute
// glyphs to their owning items. A ligature stays with its first code unit.
// BreakText can then use each item's remaining width and wrapping style.
void InlineLayoutAlgorithm::SplitShapingRuns() {
  const auto& units = result_->inline_items_;
  HeapVector<Run> split;
  split.ReserveInitialCapacity(runs_.size());
  wtf_size_t unit_index = 0;
  for (Run& run : runs_) {
    while (unit_index < units.size() && units[unit_index].end <= run.start) ++unit_index;
    assert(unit_index < units.size());
    if (!run.shape || units[unit_index].end >= run.end) {
      split.push_back(std::move(run));
      continue;
    }
    Vector<ShapeResult::ShapeRange, 8> ranges;
    const bool has_ligatures = run.shape->HasLigatures();
    if (has_ligatures) run.shape->EnsurePositionData();
    for (wtf_size_t i = unit_index; i < units.size() && units[i].start < run.end; ++i) {
      const InlineItem& unit = units[i];
      if (unit.start == unit.end) continue;
      const unsigned start = std::max(run.start, unit.start);
      const unsigned end = std::min(run.end, unit.end);
      auto shape = ShapeResult::CreateEmpty(*run.shape);
      const bool unsafe = run.unsafe_to_reuse_shape ||
                          (has_ligatures && end < run.end && run.shape->CachedNextSafeToBreakOffset(end) != end);
      ranges.emplace_back(start, end, shape.get());
      split.push_back(Run{start, end, run.level, unit.object->LayoutStyle(), unit.object,
                          std::move(shape), false, unsafe, run.shape_options});
    }
    run.shape->CopyRanges(ranges.data(), static_cast<unsigned>(ranges.size()));
  }
  runs_ = std::move(split);
}

// TextAutoSpace::Apply() (text_auto_space.cc): inserts the
// 'text-autospace' spacing between ideographs and non-ideographic letters or
// numerals. SpacingApplier assigns each opportunity to an item; the spacing
// is applied to the shaping run that holds the item's text.
// https://drafts.csswg.org/css-text-4/#propdef-text-autospace
void InlineLayoutAlgorithm::ApplyTextAutoSpace() {
  // TextAutoSpace::MayApply(): Latin-1 cannot need East Asian spacing. Keep
  // the upstream fast path despite the local buffer's UTF-16 normalization.
  if (result_->is_8bit_text_) return;
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
  // the run of the item's text. Both lists are in logical order, so consume
  // each opportunity once instead of searching the whole list for every run.
  wtf_size_t opportunity_index = 0;
  for (Run& run : runs_) {
    if (!run.shape) continue;
    Vector<OffsetWithSpacing, 16> offsets;
    while (opportunity_index < opportunities.size()) {
      const Opportunity& opportunity = opportunities[opportunity_index];
      const InlineItem& item = items[opportunity.item];
      // An opportunity at the end of its item belongs to the run before it.
      const unsigned position = opportunity.offset == item.end ? opportunity.offset - 1 : opportunity.offset;
      if (position >= run.end) break;
      ++opportunity_index;
      if (position < run.start) continue;
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
void InlineLayoutAlgorithm::ReuseCollectedItems(FragmentItems& old) {
  result_->mapping_ = old.mapping_;
  result_->is_8bit_text_ = old.is_8bit_text_;
  result_->inline_items_ = old.inline_items_;
  result_->text_combines_ = old.text_combines_;
  result_->segments_ = old.segments_;
  // Old fragments retain their run data through ShapeResultView. The shaping
  // results themselves have one owner and transfer to the replacement layout.
  runs_ = std::move(old.runs_);
  for (Run& run : runs_) run.style = run.object->LayoutStyle();
}

const InlineLayoutAlgorithm::HyphenResult& InlineLayoutAlgorithm::HyphenForStyle(const ComputedStyle& style) {
  const auto found = hyphens_.find(&style);
  if (found != hyphens_.end()) return found->value;
  // HyphenResult::Shape() uses the style's direction, including for custom
  // multi-character hyphens. Reuse the result across break trials and scoring.
  HyphenResult hyphen;
  hyphen.text = style.HyphenString().GetString();
  const auto shape = HarfBuzzShaper(hyphen.text).Shape(style.GetFont(), style.Direction());
  hyphen.shape = ShapeResultView::Create(shape.get());
  return hyphens_.insert(&style, std::move(hyphen)).stored_value->value;
}

LayoutUnit InlineLayoutAlgorithm::LineShape::SnappedWidth() const {
  return result ? LayoutUnit::FromFloatCeil(ShapeResultView::WidthForRange(*result, start, end))
                : view->SnappedWidth();
}

InlineLayoutAlgorithm::LineShape InlineLayoutAlgorithm::LineShape::SubRange(unsigned from, unsigned to) const {
  assert(from >= start && to <= end && from <= to);
  if (from == start && to == end) return *this;
  if (result) return LineShape(result, from, to);
  return LineShape(ShapeResultView::Create(view.get(), from, to));
}

std::shared_ptr<const ShapeResultView> InlineLayoutAlgorithm::LineShape::CreateView() const {
  return result ? ShapeResultView::Create(result, start, end) : view;
}

HeapVector<InlineLayoutAlgorithm::LineItem> InlineLayoutAlgorithm::ShapeLine(unsigned start, unsigned end, bool hyphenated) {
  const String& text = result_->TextContent();
  HeapVector<LineItem> items;
  // LineBreaker::PrepareNextLine(): 'text-indent' is the initial position, so
  // that tab positions align regardless of it.
  LayoutUnit advance = TextIndent(start);
  // HandleText() drops a leading collapsible space when a break at an inline
  // boundary left it for the next line instead of consuming it as trailing.
  unsigned content_start = start;
  const auto& units = result_->inline_items_;
  wtf_size_t unit_index = FirstItemEndingAfter(units, start);
  for (wtf_size_t i = unit_index; i < units.size(); ++i) {
    const auto& item = units[i];
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
  for (wtf_size_t run_index = FirstItemEndingAfter(runs_, content_start); run_index < runs_.size(); ++run_index) {
    const Run& run = runs_[run_index];
    if (run.start >= end) break;
    const unsigned run_start = std::max(content_start, run.start);
    const unsigned run_end = std::min(end, run.end);
    // HandleControlItem ignores CR/FF; they keep identity offset mappings.
    if (run.control && IsIgnoredControl(text[run_start])) continue;
    LineShape shape;
    if (run.shape) {
      LazyLineBreakIterator breaks(text, run.style->GetFont()->GetFontDescription().Locale(),
                                   LineBreakTypeFor(*run.style));
      LineShaper shaper(text, *run.style->GetFont(), *run.shape, breaks,
                        {result_->segments_.data(), result_->segments_.size()});
      shaper.SetLineStart(start);
      shaper.SetIsAfterForcedBreak(start && IsForcedBreak(text[start - 1]));
      shaper.SetTextSpacingTrim(run.style->GetFont()->GetFontDescription().GetTextSpacingTrim());
      auto range = shaper.ShapeRangeAt(run_start, run_end);
      shape = range.reusable ? LineShape(range.reusable, run_start, run_end)
                             : LineShape(std::move(range.reshaped));
    } else if (text[run_start] == '\t') {
      const auto tab_shape = ShapeResult::CreateForTabulationCharacters(run.style->GetFont(),
                                                                        DirectionFromLevel(run.level), run.style->GetTabSize(), advance.ToFloat(), run_start, 1);
      shape = LineShape(ShapeResultView::Create(tab_shape.get()));
    }
    // LineBreaker/LogicalLineBuilder advance their item index as text is
    // consumed. A bidi run can end inside an item, so retain that item for
    // the next run until its end has actually been reached.
    while (unit_index < units.size() && units[unit_index].end <= run_start) ++unit_index;
    for (wtf_size_t i = unit_index; i < units.size(); ++i) {
      const auto& unit = units[i];
      if (unit.start >= run_end) break;
      if (unit.is_generated_for_line_break || unit.type == InlineItem::kBidiControl) continue;
      const unsigned unit_end = std::min(run_end, unit.end);
      for (unsigned fragment_start = std::max(run_start, unit.start); fragment_start < unit_end;) {
        const unsigned fragment_end = unit_end;
        LineItem item;
        item.object_ = unit.object;
        item.type_ = unit.object->IsAtomicInline() ? FragmentItem::kBox : FragmentItem::kText;
        item.text_offset_ = {fragment_start, fragment_end};
        item.bidi_level_ = run.level;
        item.line_break_ = run.control && !unit.object->IsAtomicInline() && IsForcedBreak(text[run_start]);
        if (shape) {
          // As with a single InlineItemResult upstream, retain the whole view
          // when no node or trailing-space boundary actually splits it.
          if (item.text_offset_.start == shape.start && item.text_offset_.end == shape.end) {
            item.shape_ = shape;
          } else {
            item.shape_ = shape.SubRange(item.text_offset_.start, item.text_offset_.end);
          }
          item.inline_size_ = item.shape_.SnappedWidth().ClampNegativeToZero();
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
  FinishLine(items, start, end, hyphenated);
  return items;
}

void InlineLayoutAlgorithm::FinishLine(HeapVector<LineItem>& items, unsigned start, unsigned end, bool hyphenated) {
  const String& text = result_->TextContent();
  RemoveTrailingCollapsibleSpace(items, start, end);
  // Split bidi trailing whitespace using the selected view, preserving its
  // rounded total width. Placement resets the trailing part to the base level.
  unsigned trailing_space = end;
  while (trailing_space > start && (IsBidiTrailingSpace(text[trailing_space - 1]) || IsForcedBreak(text[trailing_space - 1])))
    --trailing_space;
  for (wtf_size_t i = 0; i < items.size(); ++i) {
    LineItem& item = items[i];
    const auto range = item.text_offset_;
    if (!item.shape_ || item.bidi_level_ == (IsLtr(base_direction_) ? 0u : 1u) ||
        trailing_space <= range.start || trailing_space >= range.end) continue;
    LineItem trailing = item;
    const LayoutUnit unsplit_width = item.inline_size_;
    trailing.text_offset_.start = trailing_space;
    trailing.shape_ = item.shape_.SubRange(trailing_space, range.end);
    item.text_offset_.end = trailing_space;
    item.shape_ = item.shape_.SubRange(range.start, trailing_space);
    item.inline_size_ = item.shape_.SnappedWidth().ClampNegativeToZero();
    trailing.inline_size_ = (unsplit_width - item.inline_size_).ClampNegativeToZero();
    items.insert(i + 1, std::move(trailing));
    break;
  }
  // HyphenResult::Shape and LogicalLineBuilder::PlaceHyphen. A soft hyphen
  // generates a visible hyphen only when it actually ends a wrapped line, and
  // 'hyphens: auto' when the line breaker hyphenated the last word.
  const bool soft_hyphen = IsSoftHyphenBreak(text, start, end);
  if (!items.empty() && !items.back().IsAtomicInline() && (hyphenated || soft_hyphen)) {
    LineItem hyphen;
    const auto& previous = items.back();
    hyphen.object_ = previous.object_;
    hyphen.bidi_level_ = previous.bidi_level_;
    hyphen.type_ = FragmentItem::kGeneratedText;
    hyphen.text_offset_ = {end, end};
    const HyphenResult& hyphen_result = HyphenForStyle(hyphen.Style());
    hyphen.generated_text_ = hyphen_result.text;
    hyphen.shape_ = LineShape(hyphen_result.shape);
    hyphen.inline_size_ = hyphen_result.InlineSize();
    items.push_back(std::move(hyphen));
  }
}

// LineBreaker::RemoveTrailingCollapsibleSpace(): the last text item of a
// line drops its trailing collapsible space (one space after Phase I). An item
// that becomes empty is not placed.
// https://drafts.csswg.org/css-text-3/#white-space-phase-2
void InlineLayoutAlgorithm::RemoveTrailingCollapsibleSpace(HeapVector<LineItem>& items,
                                                          unsigned start, unsigned end) const {
  const String& text = result_->TextContent();
  // Ignored CR/FF control items have no fragments, but upstream retains their
  // empty line results. Unlike forced breaks, these stop trailing collapse.
  const auto& units = result_->inline_items_;
  const auto line_end = std::lower_bound(units.begin(), units.end(), end,
                                        [](const InlineItem& item, unsigned offset) { return item.start < offset; });
  for (wtf_size_t i = line_end - units.begin(); i;) {
    const auto& item = units[--i];
    if (item.end <= start) break;
    if (item.start == item.end || item.is_generated_for_line_break ||
        item.type == InlineItem::kBidiControl)
      continue;
    if (item.type == InlineItem::kControl) {
      if (IsForcedBreak(text[item.start])) continue;
      if (IsIgnoredControl(text[item.start])) return;
    }
    break;
  }
  for (wtf_size_t i = items.size(); i;) {
    LineItem& item = items[--i];
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
      item.shape_ = item.shape_.SubRange(range.start, range.end - 1);
      item.inline_size_ = item.shape_.SnappedWidth().ClampNegativeToZero();
    }
    return;
  }
}

// LineInfo::ComputeTrailingSpaceWidth(): preserve hangs at soft wraps. On the
// last line or before a forced break, only overflowing trailing spaces hang.
// Tabs are control items upstream and participate in the same calculation.
LayoutUnit InlineLayoutAlgorithm::HangingTrailingSpaceWidth(std::span<const LineItem> items,
                                                            LayoutUnit width, bool is_last_line) const {
  const String& text = result_->TextContent();
  LayoutUnit hang;
  for (wtf_size_t i = items.size(); i;) {
    const LineItem& item = items[--i];
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
                                          : item.inline_size_ - item.shape_.SubRange(range.start, end).SnappedWidth();
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
  for (const CachedLineBreak& cached : line_breaks_) {
    if (cached.valid && cached.start == start && cached.available_width == available_width &&
        cached.direction == base_direction_) {
      return cached.result;
    }
  }
  const LayoutUnit saved_available_width = available_width_;
  available_width_ = available_width;
  const LayoutUnit available_width_to_fit = available_width.AddEpsilon();
  const String& text = result_->TextContent();
  const auto& units = result_->inline_items_;
  const unsigned limit = ParagraphEnd(start);
  LineBreakResult result{limit, false};
  HeapVector<LineItem> items;
  struct ItemResult {
    wtf_size_t run_index;
    LayoutUnit before;
    unsigned break_after = 0;
    bool hyphenated = false;
    bool may_break_inside = false;
  };
  Vector<ItemResult, 16> states;
  bool break_anywhere = false;
  bool disable_phrase = false;
  bool done = start == limit;

  const auto make_item = [&](const Run& run, unsigned from, unsigned to, LayoutUnit position) {
    LineItem item;
    item.object_ = run.object;
    item.text_offset_ = {from, to};
    item.bidi_level_ = run.level;
    item.line_break_ = run.control && IsForcedBreak(text[from]);
    if (run.shape) {
      item.shape_ = LineShape(run.shape.get(), from, to);
      item.inline_size_ = item.shape_.SnappedWidth().ClampNegativeToZero();
    } else if (run.object->IsAtomicInline()) {
      item.type_ = FragmentItem::kBox;
      item.inline_size_ = run.object->LayoutAtomicSize().inline_size;
    } else if (const auto combine = result_->text_combines_.find(run.object); combine != result_->text_combines_.end()) {
      item.type_ = FragmentItem::kBox;
      item.text_combine_ = combine->value;
      item.inline_size_ = item.text_combine_->Size().height;
    } else if (text[from] == uchar::kTab) {
      const auto shape = ShapeResult::CreateForTabulationCharacters(run.style->GetFont(), DirectionFromLevel(run.level),
                                                                   run.style->GetTabSize(), position.ToFloat(), from, 1);
      item.shape_ = LineShape(ShapeResultView::Create(shape.get()));
      item.inline_size_ = item.shape_.SnappedWidth().ClampNegativeToZero();
    }
    return item;
  };
  const auto break_after = [&](unsigned offset) {
    if (offset == limit) return offset;
    // CanBreakAfter(): inspect this boundary, including inline tags, without
    // searching the rest of an unbreakable word again for every text item.
    return NextBreakOpportunity(*breaks_, offset, offset + 1, break_anywhere, disable_phrase, false) == offset
               ? offset : 0u;
  };
  const auto break_text = [&](wtf_size_t run_index, unsigned from, LayoutUnit before,
                              LayoutUnit remaining, LayoutUnit remaining_with_hyphen) {
    const Run& run = runs_[run_index];
    const ComputedStyle& style = *run.style;
    ConfigureBreakIterator(*breaks_, style, break_anywhere, disable_phrase);
    const Hyphenation* hyphenation = disable_phrase ? style.GetHyphenationWithLimits() : HyphenationForStyle(style);
    LineShaper shaper(text, *style.GetFont(), *run.shape, *breaks_,
                      {result_->segments_.data(), result_->segments_.size()}, hyphenation);
    shaper.SetLineStart(start);
    shaper.SetIsAfterForcedBreak(start && IsForcedBreak(text[start - 1]));
    shaper.SetTextSpacingTrim(style.GetFontDescription().GetTextSpacingTrim());
    if (!break_anywhere && BreakAnywhereIfOverflow(style)) shaper.SetNoResultIfOverflow();
    LineItem item;
    item.object_ = run.object;
    item.bidi_level_ = run.level;
    ItemResult state{run_index, before};
    LayoutUnit space = remaining.ClampNegativeToZero();
    for (unsigned attempt = 0;; ++attempt) {
      ShapingLineBreaker::Result shaped;
      auto shape = shaper.ShapeLine(from, space, &shaped);
      if (!shape) {
        // Upstream's overflow sentinel avoids shaping a long word that will
        // be discarded by rewind or the whole-line break-anywhere retry.
        item.text_offset_ = {from, run.end};
        item.inline_size_ = remaining_with_hyphen.ClampNegativeToZero() + LayoutUnit(1);
        break;
      }
      assert(shaped.break_offset > from && shaped.break_offset <= run.end);
      item.text_offset_ = {from, shaped.break_offset};
      item.inline_size_ = shape->SnappedWidth().ClampNegativeToZero();
      state.hyphenated = shaped.is_hyphenated && shaped.break_offset < limit;
      state.may_break_inside = !shaped.is_overflow;
      if (!attempt && state.hyphenated && !shaped.is_overflow && item.inline_size_ <= remaining) {
        const LayoutUnit hyphen_width = HyphenForStyle(style).InlineSize();
        if (item.inline_size_ <= remaining_with_hyphen && item.inline_size_ + hyphen_width > remaining_with_hyphen) {
          space = (remaining - hyphen_width).ClampNegativeToZero();
          continue;
        }
      }
      item.shape_ = LineShape(std::move(shape));
      break;
    }
    state.break_after = item.text_offset_.end < run.end ? item.text_offset_.end : break_after(run.end);
    return std::pair(std::move(item), state);
  };
  const auto width_to_fit = [&](wtf_size_t count) {
    const ItemResult& state = states[count - 1];
    const LineItem& item = items[count - 1];
    LayoutUnit width = state.before + item.inline_size_;
    if (runs_[state.run_index].shape && !item.shape_) return width;
    if (state.hyphenated) return width + HyphenForStyle(item.Style()).InlineSize();
    return width - HangingTrailingSpaceWidth({items.data(), count}, width, item.text_offset_.end == limit);
  };
  const auto finish = [&](wtf_size_t count, unsigned end, bool hyphenated) {
    items.Shrink(count);
    LayoutUnit position = count ? states[count - 1].before + items.back().inline_size_ : TextIndent(start);
    // HandleTrailingSpaces(): ShapeLine may stop before spaces. Consume them
    // without reshaping the selected line edge; preserved spaces can hang.
    for (wtf_size_t i = FirstItemEndingAfter(runs_, end); i < runs_.size() && end < limit; ++i) {
      const Run& run = runs_[i];
      const InlineItem& unit = units[FirstItemEndingAfter(units, end)];
      if (unit.is_generated_for_line_break || IsNonFragmentItem(unit, text)) {
        end = std::min(run.end, limit);
        continue;
      }
      if (!run.style->ShouldWrapLine() || run.style->ShouldBreakSpaces() || unit.type == InlineItem::kAtomicInline) break;
      unsigned to = end;
      while (to < std::min(run.end, limit) &&
             (LazyLineBreakIterator::IsBreakableSpace(text[to]) || Character::IsOtherSpaceSeparator(text[to]))) ++to;
      if (to == end) break;
      LineItem item = make_item(run, end, to, position);
      position += item.inline_size_;
      items.push_back(std::move(item));
      end = to;
      if (end < run.end) break;
    }
    result.end = end;
    result.is_hyphenated = hyphenated || IsSoftHyphenBreak(text, start, end);
    FinishLine(items, start, end, result.is_hyphenated);
    done = true;
  };

  while (!done) {
    items.clear();
    states.clear();
    LayoutUnit position = TextIndent(start);
    bool leading = true;
    bool retry = false;
    bool overflowing = false;
    for (wtf_size_t i = FirstItemEndingAfter(runs_, start); i < runs_.size() && runs_[i].start < limit; ++i) {
      const Run& run = runs_[i];
      unsigned from = std::max(start, run.start);
      const InlineItem& unit = units[FirstItemEndingAfter(units, from)];
      if (unit.is_generated_for_line_break || IsNonFragmentItem(unit, text)) {
        // Empty results still carry break opportunities. A generated ZWSP can
        // introduce one even in nowrap; bidi/ignored controls transfer it.
        if (!states.empty()) {
          ItemResult& previous = states.back();
          if (unit.is_generated_for_line_break || previous.break_after == from)
            previous.break_after = std::min(run.end, limit);
          if (overflowing && previous.break_after) {
            finish(items.size(), previous.break_after, previous.hyphenated);
            break;
          }
        }
        continue;
      }
      if (leading && unit.type == InlineItem::kText && run.style->ShouldCollapseWhiteSpaces() && text[from] == uchar::kSpace) {
        if (++from == run.end) continue;
      }
      leading = false;
      LineItem item;
      ItemResult state{i, position};
      if (run.shape && run.style->ShouldWrapLine()) {
        const LayoutUnit remaining = available_width_to_fit - position;
        auto broken = break_text(i, from, position, remaining, remaining);
        item = std::move(broken.first);
        state = broken.second;
      } else {
        item = make_item(run, from, std::min(run.end, limit), position);
        state.break_after = break_after(item.text_offset_.end);
      }
      // Continuing to another item removes any tentative hyphen.
      position += item.inline_size_;
      items.push_back(std::move(item));
      states.push_back(state);
      if (width_to_fit(items.size()) <= available_width_to_fit) {
        if (items.back().text_offset_.end < run.end) finish(items.size(), items.back().text_offset_.end, state.hyphenated);
        if (done) break;
        continue;
      }
      if (overflowing && (!run.shape || items.back().shape_)) {
        if (state.break_after) {
          finish(items.size(), state.break_after, state.hyphenated);
          break;
        }
        continue;
      }

      // HandleOverflow(): rewind results, then rebreak only the item that can
      // contain a fitting boundary. Never remeasure an expanding line prefix.
      bool can_retry_anywhere = false;
      wtf_size_t earliest_break = 0;
      for (wtf_size_t count = states.size(); count; --count) {
        const ItemResult& previous = states[count - 1];
        const LineItem& previous_item = items[count - 1];
        can_retry_anywhere |= BreakAnywhereIfOverflow(previous_item.Style());
        if (previous.break_after) {
          earliest_break = count;
          if (width_to_fit(count) <= available_width_to_fit) {
            finish(count, previous.break_after, previous.hyphenated);
            break;
          }
        }
        const LayoutUnit remaining = available_width_to_fit - previous.before;
        const LayoutUnit shorter = previous_item.inline_size_ - LayoutUnit(1);
        if (previous.may_break_inside && remaining > LayoutUnit() && shorter <= LayoutUnit()) {
          // BreakTextAtPreviousBreakOpportunity(): zero-width text cannot be
          // shortened by reducing the available width.
          ConfigureBreakIterator(*breaks_, previous_item.Style(), break_anywhere, disable_phrase);
          const unsigned end = breaks_->PreviousBreakOpportunity(previous_item.text_offset_.end - 1,
                                                                 previous_item.text_offset_.start);
          if (end > previous_item.text_offset_.start) {
            items[count - 1] = make_item(runs_[previous.run_index], previous_item.text_offset_.start, end, previous.before);
            states[count - 1].break_after = end;
            states[count - 1].hyphenated = IsSoftHyphenBreak(text, start, end);
            finish(count, end, states[count - 1].hyphenated);
            break;
          }
        }
        if (previous.may_break_inside && remaining > LayoutUnit() && shorter > LayoutUnit()) {
          auto broken = break_text(previous.run_index, previous_item.text_offset_.start, previous.before,
                                   std::min(remaining, shorter), remaining);
          const LayoutUnit width = broken.first.inline_size_ +
                                   (broken.second.hyphenated ? HyphenForStyle(broken.first.Style()).InlineSize() : LayoutUnit());
          if (broken.first.shape_ && broken.second.break_after && width <= remaining &&
              broken.first.text_offset_.end < previous_item.text_offset_.end) {
            items[count - 1] = std::move(broken.first);
            states[count - 1] = broken.second;
            finish(count, broken.second.break_after, broken.second.hyphenated);
            break;
          }
        }
      }
      if (done) break;
      // RetryAfterOverflow(): phrase -> normal, then emergency wrapping. Both
      // retries rebuild the whole line with the new break policy.
      result.disable_score_and_bisect = true;
      if (!disable_phrase && LineBreakTypeFor(*run.style) == LineBreakType::kPhrase) {
        disable_phrase = true;
        retry = true;
      } else if (!break_anywhere && can_retry_anywhere) {
        break_anywhere = true;
        retry = true;
      }
      if (retry) break;
      if (earliest_break) {
        const ItemResult& previous = states[earliest_break - 1];
        finish(earliest_break, previous.break_after, previous.hyphenated);
        break;
      }
      // An unbreakable sequence may cross text items. Keep consuming it until
      // its first legal boundary, even when it necessarily overflows.
      overflowing = true;
    }
    if (!done && !retry) finish(items.size(), limit, false);
  }
  available_width_ = saved_available_width;
  line_breaks_[next_line_break_] = {start, available_width, base_direction_, result, std::move(items), true};
  next_line_break_ = (next_line_break_ + 1) % line_breaks_.size();
  return result;
}

unsigned InlineLayoutAlgorithm::ParagraphEnd(unsigned start) const {
  const auto it = std::lower_bound(forced_break_offsets_.begin(), forced_break_offsets_.end(), start);
  return it == forced_break_offsets_.end() ? result_->TextContent().length() : *it;
}

// LineBreaker::SetCurrentStyleForce(): break-spaces disables score breaking,
// but still permits the bisection fallback. Include style changes on empty
// tags and the block style used to initialize the first line.
bool InlineLayoutAlgorithm::UsesBreakSpaces(unsigned start, unsigned end) const {
  const auto& items = result_->inline_items_;
  const String& text = result_->TextContent();
  const auto first = std::lower_bound(items.begin(), items.end(), start,
                                     [](const InlineItem& item, unsigned offset) { return item.end < offset; });
  const auto uses_break_spaces = [](const ComputedStyle& style) {
    return style.ShouldWrapLine() && style.ShouldBreakSpaces();
  };
  if (!start && uses_break_spaces(context_.root_->Style())) return true;
  const wtf_size_t first_index = first - items.begin();
  for (wtf_size_t i = first_index; i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.start > end || (item.start == end && item.type != InlineItem::kCloseTag)) break;
    // A nonempty item ending at start belongs to the preceding line. In
    // particular, a preserved newline must not disable the next paragraph.
    if (item.start != item.end && item.end <= start) continue;
    if (item.type != InlineItem::kOpenTag && item.type != InlineItem::kCloseTag &&
        (item.start == item.end || IsNonFragmentItem(item, text))) continue;
    if (uses_break_spaces(*break_styles_before_[i + 1])) return true;
  }
  return false;
}

// ScoreLineBreaker::OptimalBreakPoints(): computes `break_points_` for the
// paragraph that starts at `start` when it ends within MaxLines() greedy lines
// and the optimization applies.
bool InlineLayoutAlgorithm::ComputeScoreBreakPoints(unsigned start, bool is_balanced) {
  // InlineItemsBuilder disables scoring for the whole node when a preserved
  // tab is collected; tab advances depend on the start of each candidate line.
  if (has_preserved_tabs_ || (score_suspended_until_ && start <= *score_suspended_until_)) return false;
  constexpr wtf_size_t kMaxLinesForBalance = 6;
  constexpr wtf_size_t kMaxLinesForOptimal = 4;
  const wtf_size_t max_lines = is_balanced ? kMaxLinesForBalance : kMaxLinesForOptimal;
  const String& text = result_->TextContent();
  const unsigned limit = ParagraphEnd(start);
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
    const unsigned line_end = result.end == limit && limit < text.length() ? limit + 1 : result.end;
    if (result.disable_score_and_bisect || UsesBreakSpaces(line_start, line_end)) {
      score_suspended_until_ = limit;
      return false;
    }
    lines.push_back(GreedyLine{line_start, result.end, result.is_hyphenated});
    if (result.end <= line_start) return false;
    line_start = result.end;
  }
  // Upstream suspends after reaching the paragraph end, even if scoring is
  // unnecessary or fails. Its remaining greedy lines are then consumed as-is.
  score_suspended_until_ = limit;
  // Optimization not needed for single line paragraphs.
  if (lines.size() <= 1) return false;
  const ComputedStyle& block_style = context_.root_->Style();
  if (!is_balanced) {
    // ShouldOptimize(): the last line is short and a single word, or the two
    // lines before it are hyphenated.
    const GreedyLine& last_line = lines.back();
    const LayoutUnit last_width = Measure(last_line.start, last_line.end);
    constexpr int kShortLineDenominator = 3;
    constexpr wtf_size_t kNumLastHyphenatedLines = 2;
    const wtf_size_t num_lines = lines.size();
    const bool short_last_line = last_width < options_.available_inline_size / kShortLineDenominator &&
                                NextBreakOpportunity(*breaks_, last_line.start + 1, last_line.end) >= last_line.end;
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

  // AppendCandidates() uses cached character positions and a base position
  // for each item. Keep the bases for this paragraph so queries at trimmed
  // ends and hyphenation points do not measure every preceding run again.
  const auto first_run = runs_.begin() + FirstItemEndingAfter(runs_, start);
  const auto last_run = std::lower_bound(first_run, runs_.end(), limit,
                                       [](const Run& run, unsigned offset) { return run.start < offset; });
  const std::span<const Run> paragraph_runs(first_run, last_run);
  Vector<float> run_positions;
  run_positions.ReserveInitialCapacity(paragraph_runs.size() + 1);
  float position = 0;
  for (const Run& run : paragraph_runs) {
    run_positions.push_back(position);
    const unsigned run_start = std::max(start, run.start);
    const unsigned run_end = std::min(limit, run.end);
    if (run.shape) {
      run.shape->EnsurePositionData();
      // Match ShapeResultWrapper's LayoutUnit positions, including RTL.
      position += run.shape->CachedWidth(run_start, run_end).ToFloat();
    } else if (run.object->IsAtomicInline()) {
      position += run.object->LayoutAtomicSize().inline_size.ToFloat();
    } else if (const auto combine = result_->text_combines_.find(run.object);
               combine != result_->text_combines_.end()) {
      position += combine->value->Size().height.ToFloat();
    } else if (text[run_start] == uchar::kTab) {
      const auto tab = ShapeResult::CreateForTabulationCharacters(run.style->GetFont(), DirectionFromLevel(run.level),
                                                                 run.style->GetTabSize(), position, run_start, 1);
      position += tab->Width();
    }
  }
  run_positions.push_back(position);
  const auto content_width = [&](unsigned offset) {
    assert(start <= offset && offset <= limit);
    const wtf_size_t index = FirstItemEndingAfter(paragraph_runs, offset);
    const float base = run_positions[index];
    if (index == paragraph_runs.size()) return base;
    const Run& run = paragraph_runs[index];
    // Non-text runs cover one code unit and contribute at their end only.
    if (!run.shape) return base;
    return base + run.shape->CachedWidth(std::max(start, run.start), offset).ToFloat();
  };
  const auto position_if_break = [&](unsigned from, unsigned end) {
    if (end <= from) return content_width(end);
    // Select the run containing the character before the break, even when
    // end is exactly a run boundary. Its position data was populated above.
    const wtf_size_t run_index = FirstItemEndingAfter(paragraph_runs, end - 1);
    assert(run_index < paragraph_runs.size());
    const Run& run = paragraph_runs[run_index];
    if (!run.shape) return content_width(end);
    unsigned safe = run.shape->CachedPreviousSafeToBreakOffset(end);
    if (safe == end) return content_width(end);
    // AppendCandidates() reshapes only the unsafe suffix, clipped to the
    // current word/item. A local shaping run may span several inline items.
    const auto& units = result_->inline_items_;
    const wtf_size_t item_index = FirstItemEndingAfter(units, end - 1);
    assert(item_index < units.size());
    const InlineItem& item = units[item_index];
    // InlineNode splits the shared shape at item boundaries; an item's
    // ShapeResult always treats its EndIndex() as safe to break.
    if (end == item.end) return content_width(end);
    safe = std::max({safe, from, run.start, item.start});
    assert(safe < end);
    const auto shape = ReshapeText(text, *run.style->GetFont(), DirectionFromLevel(run.level), safe, end,
                                   {result_->segments_.data(), result_->segments_.size()});
    // Upstream adds the raw reshaped advance to the cached base position.
    return content_width(safe) + shape->Width();
  };

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
    // AppendCandidates() excludes only SPACE/TAB/LF. Other bidi whitespace,
    // such as EM SPACE, still contributes to the width of a broken line.
    while (end > from && LazyLineBreakIterator::IsBreakableSpace(text[end - 1])) --end;
    return end;
  };
  for (unsigned previous = start; previous < limit;) {
    const unsigned opportunity = NextBreakOpportunity(*breaks_, previous + 1, limit);
    // Hyphenation opportunities of the word before this break opportunity.
    unsigned word_start = previous;
    while (word_start < opportunity && LazyLineBreakIterator::IsBreakableSpace(text[word_start])) ++word_start;
    const unsigned word_end = trimmed_end(word_start, opportunity);
    if (word_start < word_end) {
      const ComputedStyle& style = StyleAtOffset(word_start);
      if (const Hyphenation* hyphenation = HyphenationForStyle(style)) {
        const Vector<wtf_size_t, 8> locations =
            hyphenation->HyphenLocations(StringView(text, word_start, word_end - word_start));
        if (!locations.empty()) {
          const float hyphen_width = HyphenForStyle(style).InlineSize().ToFloat();
          for (wtf_size_t i = locations.size(); i-- > 0;) {
            const unsigned offset = word_start + locations[i];
            const float position = content_width(offset);
            candidates.push_back(Candidate{offset, position, position + hyphen_width, hyphen_penalty, true});
          }
        }
      }
    }
    float break_position = position_if_break(previous, trimmed_end(start, opportunity));
    if (opportunity >= limit) {
      candidates.push_back(Candidate{limit, break_position, break_position, 0, false});
      break;
    }
    const bool is_hyphenated = IsSoftHyphenBreak(text, start, opportunity);
    if (is_hyphenated) {
      // AppendCandidates(): a soft-hyphen opportunity has the same extra
      // advance and penalty as an automatically hyphenated candidate.
      break_position += HyphenForStyle(StyleAtOffset(opportunity - 1)).InlineSize().ToFloat();
    }
    candidates.push_back(Candidate{opportunity, content_width(opportunity),
                          break_position, is_hyphenated ? hyphen_penalty : 0, is_hyphenated});
    previous = opportunity;
  }

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
  if (!forced_break_offsets_.empty()) return std::nullopt;
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
  // exceeds `max_lines` or is inapplicable; `width_sum` receives their widths.
  const auto break_lines = [&](LayoutUnit width, wtf_size_t max_lines, LayoutUnit* width_sum) -> std::optional<wtf_size_t> {
    wtf_size_t num_lines = 0;
    for (unsigned line_start = 0; line_start < text.length();) {
      if (num_lines >= max_lines) return std::nullopt;
      const LineBreakResult result = BreakLine(line_start, width);
      if (result.disable_score_and_bisect) return std::nullopt;
      if (width_sum) {
        const LayoutUnit saved_width = available_width_;
        available_width_ = width;
        *width_sum += Measure(line_start, result.end, nullptr, result.is_hyphenated);
        available_width_ = saved_width;
      }
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
  const auto& items = result_->inline_items_;
  for (wtf_size_t i = FirstItemEndingAfter(items, offset); i < items.size(); ++i) {
    const auto& item = items[i];
    if (item.type == InlineItem::kText && item.start <= offset && offset < item.end) return item.object->Style();
    if (item.start > offset) break;
  }
  return context_.root_->Style();
}

InlineLayoutAlgorithm::ShapedLine& InlineLayoutAlgorithm::GetShapedLine(unsigned start, unsigned end, bool hyphenated) {
  // Measurement discovers soft hyphens implicitly; placement receives the
  // line break flag. Both requests must hit the same shaped-line entry.
  hyphenated |= IsSoftHyphenBreak(result_->TextContent(), start, end);
  for (unsigned i = 0; i < shaped_lines_.size(); ++i) {
    ShapedLine& line = shaped_lines_[i];
    if (line.valid && line.start == start && line.end == end && line.direction == base_direction_ &&
        line.hyphenated == hyphenated) {
      next_shaped_line_ = i ^ 1;
      return line;
    }
  }
  for (unsigned i = 0; i < shaped_lines_.size(); ++i) {
    if (!shaped_lines_[i].valid) {
      next_shaped_line_ = i;
      break;
    }
  }
  ShapedLine& line = shaped_lines_[next_shaped_line_];
  next_shaped_line_ ^= 1;
  line = {.items = ShapeLine(start, end, hyphenated),
          .start = start,
          .end = end,
          .direction = base_direction_,
          .hyphenated = hyphenated,
          .valid = true};
  return line;
}

LayoutUnit InlineLayoutAlgorithm::Measure(unsigned start, unsigned end, bool* has_content, bool hyphenated) {
  hyphenated |= IsSoftHyphenBreak(result_->TextContent(), start, end);
  const HeapVector<LineItem>* selected_items = nullptr;
  for (const CachedLineBreak& cached : line_breaks_) {
    if (cached.valid && cached.start == start && cached.result.end == end &&
        cached.available_width == available_width_ && cached.direction == base_direction_ &&
        cached.result.is_hyphenated == hyphenated) {
      selected_items = &cached.items;
      break;
    }
  }
  const auto& items = selected_items ? *selected_items : GetShapedLine(start, end, hyphenated).items;
  if (has_content) *has_content = !items.empty();
  // LineInfo::ComputeWidth() includes 'text-indent'.
  LayoutUnit width = TextIndent(start);
  for (const auto& item : items) width += item.inline_size_;
  const String& text = result_->TextContent();
  const bool is_last_line = end == text.length() || IsForcedBreak(text[end]);
  return width - HangingTrailingSpaceWidth({items.data(), items.size()}, width, is_last_line);
}

// LineBreaker::PrepareNextLine(): 'text-indent' applies to the first formatted
// line of the block.
LayoutUnit InlineLayoutAlgorithm::TextIndent(unsigned line_start) const {
  if (line_start) return LayoutUnit();
  return MinimumValueForLength(context_.root_->Style().TextIndent(), options_.available_inline_size);
}

// LineBreaker::SetCurrentStyle(), HandleOpenTag(), HandleCloseTag() and
// CanBreakAfterAtomicInline(). Container boundaries can add opportunities,
// even when the adjacent text itself has nowrap.
unsigned InlineLayoutAlgorithm::NextBreakOpportunity(LazyLineBreakIterator& breaks, unsigned offset, unsigned limit,
                                                     bool break_anywhere, bool disable_phrase, bool advance_empty) const {
  const String& text = result_->TextContent();
  const unsigned length = text.length();
  const auto& items = result_->inline_items_;
  // Keep items ending exactly at the requested offset: their trailing
  // opportunity and any open/close tags there still participate in breaking.
  const auto first = std::lower_bound(items.begin(), items.end(), offset,
                                     [](const InlineItem& item, unsigned offset) { return item.end < offset; });
  if (first == items.end()) return limit;
  const wtf_size_t first_index = first - items.begin();
  const ComputedStyle* current_style = break_styles_before_[first_index];
  const auto set_style = [&](const ComputedStyle& style) {
    current_style = &style;
    if (style.ShouldWrapLine()) ConfigureBreakIterator(breaks, style, break_anywhere, disable_phrase);
  };
  const auto is_space = [](UChar c) { return c == uchar::kSpace || c == uchar::kTab; };
  // Empty results transfer the preceding opportunity. Open tags start a new
  // result without transferring it; close tags propagate it unconditionally.
  const auto advance_past_empty = [&](wtf_size_t i, unsigned candidate) {
    if (!advance_empty) return candidate;
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
  for (wtf_size_t i = first_index; i < items.size(); ++i) {
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
      std::unique_ptr<ShapeResult> shape_result = item.shape_->CreateShapeResult();
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

void InlineLayoutAlgorithm::PlaceLine(unsigned start, unsigned end, bool soft_wrap, bool hyphenated, bool use_greedy_result) {
  HeapVector<LineItem> line_items;
  if (use_greedy_result) {
    const String& text = result_->TextContent();
    const bool forced = end > start && IsForcedBreak(text[end - 1]);
    const unsigned break_end = forced ? end - 1 : end;
    bool found = false;
    for (CachedLineBreak& cached : line_breaks_) {
      if (!cached.valid || cached.start != start || cached.available_width != available_width_ ||
          cached.direction != base_direction_ || cached.result.end != break_end) continue;
      line_items = std::move(cached.items);
      cached.valid = false;
      found = true;
      if (forced) {
        auto newline = ShapeLine(break_end, end);
        for (LineItem& item : newline) line_items.push_back(std::move(item));
      }
      break;
    }
    assert(found);
  } else {
    ShapedLine& shaped_line = GetShapedLine(start, end, hyphenated);
    line_items = std::move(shaped_line.items);
    shaped_line.valid = false;
  }
  const LayoutUnit text_indent = TextIndent(start);
  LayoutUnit width = text_indent;
  for (const LineItem& item : line_items) width += item.inline_size_;
  const bool is_last_line = !soft_wrap;
  const LayoutUnit hang_width = HangingTrailingSpaceWidth({line_items.data(), line_items.size()}, width, is_last_line);
  HeapVector<FragmentItem> items;
  items.ReserveInitialCapacity(line_items.size());
  for (LineItem& measured : line_items) {
    FragmentItem item;
    item.object_ = measured.object_;
    item.style_ = measured.object_->LayoutStyle();
    item.type_ = measured.type_;
    item.text_offset_ = measured.text_offset_;
    item.bidi_level_ = measured.bidi_level_;
    item.line_break_ = measured.line_break_;
    item.inline_size_ = measured.inline_size_;
    if (measured.shape_) item.shape_ = measured.shape_.CreateView();
    item.text_combine_ = std::move(measured.text_combine_);
    item.generated_text_ = std::move(measured.generated_text_);
    items.push_back(std::move(item));
  }
  const String& text = result_->TextContent();
  const auto& root_style = context_.root_->LayoutStyle();
  const FontBaseline baseline_type = root_style->GetFontBaseline();

  // LineInfo: width (including 'text-indent'), hanging trailing spaces and
  // the text-align of the line.
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
  // Placement is sequential even when measurement seeks backwards. Keep its
  // own cursor and open-tag stack, as LogicalLineBuilder keeps box states
  // across lines, instead of replaying the entire prefix at every line.
  for (; continuing_box_item_index_ < units.size(); ++continuing_box_item_index_) {
    const InlineItem& unit = units[continuing_box_item_index_];
    if (unit.start > start || (unit.start == start && unit.type != InlineItem::kCloseTag)) break;
    if (unit.type == InlineItem::kOpenTag) continuing_boxes_.push_back(unit.object);
    else if (unit.type == InlineItem::kCloseTag && !continuing_boxes_.empty()) continuing_boxes_.pop_back();
  }
  wtf_size_t unit_index = continuing_box_item_index_;
  InlineLayoutStateStack box_states;
  HeapVector<FragmentItem> placed;
  placed.ReserveInitialCapacity(items.size());
  InlineBoxState* box = box_states.OnBeginPlaceItems(*context_.root_, continuing_boxes_, baseline_type, placed);
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
  // Upstream skips reordering when bidi is disabled. For an all-level-zero
  // LTR line, both L1's trailing-space reset and L2's permutation are identity.
  Vector<int32_t, 32> visual;
  if (!items.empty() &&
      (!IsLtr(base_direction_) ||
       std::any_of(items.begin(), items.end(), [](const FragmentItem& item) { return item.bidi_level_ != 0; }))) {
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
    visual.resize(items.size());
    BidiParagraph::IndicesInVisualOrder(levels, visual);
  }

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
  for (wtf_size_t i = 0; i < items.size(); ++i) {
    const wtf_size_t index = visual.empty() ? i : static_cast<wtf_size_t>(visual[i]);
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
    if (!SameSegmentation({old->segments_.data(), old->segments_.size()},
                          {result_->segments_.data(), result_->segments_.size()}, end, line_end)) {
      break;
    }
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
      if (item.text_offset_.start < item.text_offset_.end) {
        for (wtf_size_t r = FirstItemEndingAfter(runs_, item.text_offset_.start);
             r < runs_.size() && runs_[r].start < item.text_offset_.end; ++r) {
          if (runs_[r].level != item.bidi_level_) {
            reusable = false;
            break;
          }
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
    result_->is_8bit_text_ = text.Is8Bit() && !is_bidi_enabled;
    text.Ensure16Bit();
    if (!mapping_builder_.SetDestinationString(text)) NOTREACHED();
    result_->mapping_ = mapping_builder_.Build(*context_.root_);
    SegmentAndShape(use_latin1_script, is_bidi_enabled);
    SplitShapingRuns();
    ApplyTextAutoSpace();
  } else {
    ReuseCollectedItems(*context_.fragments_);
  }
  const String& text = result_->TextContent();
  const ComputedStyle& block_style = context_.root_->Style();
  // Forced breaks already have their own control runs. Index them once so
  // every greedy/score candidate need not scan the remaining paragraph.
  for (const Run& run : runs_) {
    if (run.control && IsForcedBreak(text[run.start])) forced_break_offsets_.push_back(run.start);
    if (run.control && text[run.start] == uchar::kTab) has_preserved_tabs_ = true;
  }
  // LineBreaker carries the current style in its item cursor. Measurement
  // here can revisit arbitrary offsets (hyphenation and paragraph balancing),
  // so retain the incoming style at each item for the same state after a seek.
  const ComputedStyle* break_style = &block_style;
  break_styles_before_.ReserveInitialCapacity(result_->inline_items_.size() + 1);
  for (const InlineItem& item : result_->inline_items_) {
    break_styles_before_.push_back(break_style);
    if (item.type == InlineItem::kOpenTag) {
      break_style = &item.object->Style();
    } else if (item.type == InlineItem::kCloseTag) {
      break_style = &item.object->Parent()->Style();
    } else if (item.start != item.end && !IsNonFragmentItem(item, text)) {
      break_style = item.type == InlineItem::kAtomicInline ? &item.object->Parent()->Style() : &item.object->Style();
    }
  }
  break_styles_before_.push_back(break_style);
  result_->writing_mode_ = block_style.GetWritingMode();
  result_->direction_ = block_style.Direction();
  const bool plaintext = block_style.GetUnicodeBidi() == UnicodeBidi::kPlaintext;
  unsigned start = ReuseLines();
  const auto* locale = block_style.GetFont()->GetFontDescription().Locale();
  breaks_.emplace(text, locale);
  base_direction_ = block_style.Direction();
  std::optional<unsigned> bidi_paragraph_end;
  const auto update_base_direction = [&](unsigned line_start) {
    // 'unicode-bidi: plaintext': each paragraph takes its direction from its
    // first strong character (BidiParagraph::BaseDirectionForStringOrLtr).
    if (!plaintext) return;
    if (bidi_paragraph_end && line_start <= *bidi_paragraph_end) return;
    const auto it = std::lower_bound(forced_break_offsets_.begin(), forced_break_offsets_.end(), line_start);
    const unsigned paragraph_start = it == forced_break_offsets_.begin() ? 0 : *(it - 1) + 1;
    bidi_paragraph_end = it == forced_break_offsets_.end() ? text.length() : *it;
    base_direction_ = BidiParagraph::BaseDirectionForStringOrLtr(StringView(text, paragraph_start), IsLineFeed);
  };

  // LineBreakStrategy: 'text-wrap-style: balance' balances the first
  // paragraph of the block (ScoreLineBreaker), or the whole block by bisecting
  // the available width; 'pretty' optimizes the last lines of each paragraph.
  const TextWrapStyle text_wrap = block_style.GetTextWrapStyle();
  available_width_ = options_.available_inline_size;
  if (text_wrap == TextWrapStyle::kBalance && !start && block_style.ShouldWrapLine()) {
    // LineBreaker starts with the paragraph's resolved direction. Balancing
    // runs before the normal line loop, so resolve plaintext direction here too.
    update_base_direction(start);
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
    const bool use_greedy_result = break_point_index_ >= break_points_.size();
    if (!use_greedy_result) {
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
    const unsigned limit = ParagraphEnd(start);
    const bool forced = end == limit && limit < text.length();
    if (forced) ++end;
    assert(end > start);
    const LayoutUnit saved_available_width = available_width_;
    available_width_ = line_available_width;
    PlaceLine(start, end, !forced && end < text.length(), hyphenated, use_greedy_result);
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
  return std::move(result_);
}

} // namespace bkit
