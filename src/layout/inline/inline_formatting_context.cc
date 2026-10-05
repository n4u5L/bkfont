// Local ownership and editing-geometry adapter for Blink's inline fragments.
#include "inline_formatting_context.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

#include "base/text/string_builder.h"
#include "editing/bidi_adjustment.h"
#include "font/font_cache.h"
#include "font/text_fragment_paint_info.h"
#include "layout/inline/inline_caret_position.h"
#include "layout/inline/inline_layout_algorithm.h"
#include "paint/text_fragment_painter.h"

namespace bkfont {

InlineFormattingContext::InlineFormattingContext(const InlineStyle& style, InlineLayoutOptions options)
    : options_(options) {
  options_.available_inline_size = options_.available_inline_size.ClampNegativeToZero();
  root_.reset(new InlineObject(*this, next_id_++, InlineObject::Type::kInline,
                               std::make_shared<const InlineStyle>(style)));
  objects_.emplace(root_->Id(), root_.get());
  FontCache::Get().AddClient(this);
}

InlineFormattingContext::~InlineFormattingContext() {
  epoch_.reset();
  fragments_.reset();
  retired_.clear();
}

void InlineFormattingContext::FontCacheInvalidated() {
  // Device-scale changes may change hinting and advances even when the
  // logical viewport and styles are unchanged. No old line can be reused.
  MarkDirty(*root_);
  if (fragments_) fragments_->DirtyFirstItem();
}

const InlineObject* InlineFormattingContext::Find(InlineNodeId id) const {
  const auto found = objects_.find(id);
  return found == objects_.end() ? nullptr : found->second;
}

void InlineFormattingContext::Validate(const InlineObject& object) const {
  assert(&object.Root() == this && object.IsAttached() && State() != InlineLayoutState::kInLayout);
}

void InlineFormattingContext::MarkDirty(const InlineObject& object) {
  Validate(object);
  if (fragments_) fragments_->DirtyLinesFromChangedChild(object);
  epoch_->state = InlineLayoutState::kDirty;
  ++epoch_->generation;
}

InlineObject& InlineFormattingContext::Add(const InlineObject& parent, InlineObject::Type type,
                                           std::shared_ptr<const InlineStyle> style) {
  Validate(parent);
  assert(parent.IsInline());
  // Copy client-supplied styles so mutation through another shared_ptr cannot
  // silently alter a clean layout or an old snapshot.
  if (style) style = std::make_shared<const InlineStyle>(*style);
  auto child = std::unique_ptr<InlineObject>(new InlineObject(*this, next_id_++, type, std::move(style)));
  child->parent_ = const_cast<InlineObject*>(&parent);
  InlineObject& result = *child;
  objects_.emplace(child->Id(), child.get());
  child->parent_->children_.push_back(std::move(child));
  MarkDirty(result);
  return result;
}

const InlineObject& InlineFormattingContext::AppendText(const InlineObject& parent, const String& text,
                                                        std::shared_ptr<const InlineStyle> style) {
  InlineObject& child = Add(parent, InlineObject::Type::kText, std::move(style));
  child.text_ = text;
  return child;
}

const InlineObject& InlineFormattingContext::AppendInline(const InlineObject& parent,
                                                          std::shared_ptr<const InlineStyle> style) {
  return Add(parent, InlineObject::Type::kInline, std::move(style));
}

const InlineObject& InlineFormattingContext::AppendAtomic(const InlineObject& parent, LogicalSize size,
                                                          LayoutUnit baseline, std::shared_ptr<const InlineStyle> style) {
  InlineObject& child = Add(parent, InlineObject::Type::kAtomic, std::move(style));
  child.atomic_size_ = {size.inline_size.ClampNegativeToZero(), size.block_size.ClampNegativeToZero()};
  child.atomic_baseline_ = baseline;
  return child;
}

void InlineFormattingContext::ReplaceText(const InlineObject& object, unsigned offset, unsigned length,
                                          const String& replacement) {
  Validate(object);
  assert(object.IsText() && offset <= object.Text().length() && length <= object.Text().length() - offset);
  if (!length && replacement.empty()) return;
  if (fragments_) fragments_->DirtyTextRange(object, offset);
  epoch_->state = InlineLayoutState::kDirty;
  ++epoch_->generation;
  StringBuilder builder;
  builder.Append(StringView(object.Text(), 0, offset));
  builder.Append(replacement);
  builder.Append(StringView(object.Text(), offset + length));
  const_cast<InlineObject&>(object).text_ = builder.ToString();
}

void InlineFormattingContext::SetStyle(const InlineObject& object, const InlineStyle& style) {
  MarkDirty(object);
  const_cast<InlineObject&>(object).style_ = std::make_shared<const InlineStyle>(style);
}

void InlineFormattingContext::Retire(InlineObject& object) {
  object.attached_ = false;
  objects_.erase(object.Id());
  for (auto& child : object.children_) Retire(*child);
}

void InlineFormattingContext::Remove(const InlineObject& object) {
  Validate(object);
  assert(object.parent_);
  MarkDirty(object);
  auto& children = object.parent_->children_;
  const auto it = std::find_if(children.begin(), children.end(), [&](const auto& child) { return child.get() == &object; });
  Retire(**it);
  // Preserve the detached subtree's structure while old fragments reference it.
  retired_.push_back(std::move(*it));
  children.erase(it);
}

bool InlineFormattingContext::Move(const InlineObject& object, const InlineObject& parent, const InlineObject* before) {
  Validate(object);
  Validate(parent);
  if (!object.parent_ || !parent.IsInline() || &object == &parent || parent.IsDescendantOf(object) ||
      (before && before->Parent() != &parent)) return false;
  if (before == &object) return true;
  MarkDirty(object);
  auto& source = object.parent_->children_;
  const auto from = std::find_if(source.begin(), source.end(), [&](const auto& child) { return child.get() == &object; });
  auto moving = std::move(*from);
  source.erase(from);
  auto& destination = const_cast<InlineObject&>(parent).children_;
  const auto to = before ? std::find_if(destination.begin(), destination.end(),
                                        [&](const auto& child) { return child.get() == before; })
                         : destination.end();
  moving->parent_ = const_cast<InlineObject*>(&parent);
  destination.insert(static_cast<wtf_size_t>(to - destination.begin()), std::move(moving));
  // A move can change bidi context and inherited style before either endpoint.
  if (fragments_) fragments_->DirtyFirstItem();
  return true;
}

void InlineFormattingContext::SetOptions(InlineLayoutOptions options) {
  options.available_inline_size = options.available_inline_size.ClampNegativeToZero();
  if (options_ == options) return;
  options_ = options;
  MarkDirty(*root_);
  if (fragments_) fragments_->DirtyFirstItem();
}

bool InlineFormattingContext::SetZoomFactors(float device_scale_factor, float page_zoom_factor) {
  const DeviceScale scale{device_scale_factor};
  const float zoom = device_scale_factor * page_zoom_factor;
  if (!scale.IsValid() || !std::isfinite(page_zoom_factor) || page_zoom_factor <= 0 ||
      !std::isfinite(zoom) || zoom <= 0) return false;
  Validate(*root_);
  const bool changed = device_scale_.factor != device_scale_factor || page_zoom_factor_ != page_zoom_factor;
  device_scale_ = scale;
  page_zoom_factor_ = page_zoom_factor;
  // Font render-style selection uses DSF alone, not page zoom. The computed
  // font size uses their product, as in WebFrameWidget/LocalFrame/FontBuilder.
  const bool fonts_changed = FontCache::UpdateDeviceScaleFactor(device_scale_factor);
  if (changed) {
    MarkDirty(*root_);
    if (fragments_) fragments_->DirtyFirstItem();
  }
  return changed || fonts_changed;
}

void InlineFormattingContext::UpdateLayout() {
  assert(State() != InlineLayoutState::kInLayout);
  // LayoutView::LayoutRoot activates its Page's DSF before font lookup. Other
  // contexts may have used this thread since the last visual-properties update.
  FontCache::UpdateDeviceScaleFactor(device_scale_.factor);
  if (State() == InlineLayoutState::kClean) return;
  epoch_->state = InlineLayoutState::kInLayout;
  auto next = InlineLayoutAlgorithm(*this).Layout();
  // Finalize has already rebuilt first-item indices, deltas and fragment ids.
  // Destroy the old index AND items before any address can be recycled.
  fragments_ = std::move(next);
  retired_.clear();
  ++epoch_->generation;
  epoch_->state = InlineLayoutState::kClean;
}

const FragmentItems& InlineFormattingContext::Fragments() {
  UpdateLayout();
  return *fragments_;
}

bool InlineFormattingContext::HasInlineFragments(const InlineObject& object) const {
  return State() == InlineLayoutState::kClean && fragments_ &&
         fragments_->FirstInlineFragmentItemIndex(object) != 0;
}

namespace {

float AxisDistance(float position, LayoutUnit begin, LayoutUnit end) {
  return std::max({begin.ToFloat() - position, position - end.ToFloat(), 0.0f});
}

// FragmentItem::LocalRect() and the LTR WritingModeConverter used by
// ComputeLogicalCaretRectAtTextOffset(). Positions are relative to the item.
PhysicalRect InlineRangeRect(const FragmentItem& item,
                             LayoutUnit start_position, LayoutUnit end_position, WritingMode writing_mode) {
  const auto& rect = item.RectInContainerFragment();
  const LayoutUnit inline_size = end_position - start_position;
  if (IsHorizontalWritingMode(writing_mode)) return {rect.X() + start_position, rect.Y(), inline_size, rect.Height()};
  const LayoutUnit top = writing_mode == WritingMode::kSidewaysLr ? rect.Bottom() - end_position : rect.Y() + start_position;
  return {rect.X(), top, rect.Width(), inline_size};
}

// InlineCursor::ExpandSelectionRectToLineHeight() takes the union in the
// block direction; text can extend outside a line with negative leading.
PhysicalRect ExpandSelectionRectToLineHeight(PhysicalRect rect, const FragmentItem& line,
                                             WritingMode writing_mode) {
  const auto& line_rect = line.RectInContainerFragment();
  if (IsHorizontalWritingMode(writing_mode)) {
    const LayoutUnit top = std::min(rect.Y(), line_rect.Y());
    const LayoutUnit bottom = std::max(rect.Bottom(), line_rect.Bottom());
    rect.SetY(top);
    rect.SetHeight(bottom - top);
  } else {
    const LayoutUnit left = std::min(rect.X(), line_rect.X());
    const LayoutUnit right = std::max(rect.Right(), line_rect.Right());
    rect.SetX(left);
    rect.SetWidth(right - left);
  }
  return rect;
}

// FragmentItem::LineLeftAndRightForOffsets(): the unrounded caret positions
// are snapped outwards, then ordered.
std::pair<LayoutUnit, LayoutUnit> LineLeftAndRightForPositions(float unrounded_start_position,
                                                               float unrounded_end_position) {
  LayoutUnit start_position;
  LayoutUnit end_position;
  if (unrounded_start_position > unrounded_end_position) [[unlikely]] {
    start_position = LayoutUnit::FromFloatCeil(unrounded_start_position);
    end_position = LayoutUnit::FromFloatFloor(unrounded_end_position);
  } else {
    start_position = LayoutUnit::FromFloatFloor(unrounded_start_position);
    end_position = LayoutUnit::FromFloatCeil(unrounded_end_position);
  }

  // Swap positions if RTL.
  if (start_position > end_position) [[unlikely]] {
    return std::make_pair(end_position, start_position);
  }
  return std::make_pair(start_position, end_position);
}

// From caret_rect.cc.
LayoutUnit ClampAndRound(LayoutUnit value, LayoutUnit min, LayoutUnit max) {
  LayoutUnit min_ceil = LayoutUnit(min.Ceil());
  LayoutUnit max_floor = LayoutUnit(max.Floor());
  if (min_ceil >= max_floor) return max_floor;
  return LayoutUnit(std::clamp(value, min_ceil, max_floor).Round());
}

} // namespace

PhysicalRect InlineFormattingContext::CaretRect(InlinePosition position) {
  // LocalFrameView::BarCaretWidth: one DIP, at least one framebuffer pixel.
  // Page zoom changes text size but does not enlarge the insertion bar.
  return CaretRect(position, LayoutUnit(std::max(1.f, device_scale_.factor)));
}

PhysicalRect InlineFormattingContext::CaretRect(InlinePosition position, LayoutUnit caret_width) {
  const auto caret = ComputeInlineCaretPosition(*this, position);
  if (!caret) return {};
  const auto& item = *caret.cursor.Current();
  const auto& line = (*fragments_)[item.LineIndex()];
  if (caret.position_type == InlineCaretPositionType::kBeforeBox ||
      caret.position_type == InlineCaretPositionType::kAfterBox) {
    // ComputeLocalCaretRectByBoxSide(): the far edge is inset by the caret
    // width. The bar spans the line, without text-caret clamping or rounding.
    const bool before = caret.position_type == InlineCaretPositionType::kBeforeBox;
    const LayoutUnit caret_left = IsLtr(item.ResolvedDirection()) != before
                                      ? item.InlineSize() - caret_width
                                      : LayoutUnit();
    PhysicalRect rect = InlineRangeRect(item, caret_left, caret_left + caret_width, options_.writing_mode);
    const auto& line_rect = line.RectInContainerFragment();
    if (IsHorizontalWritingMode(options_.writing_mode)) {
      rect.SetY(line_rect.Y());
      rect.SetHeight(line_rect.Height());
    } else {
      rect.SetX(line_rect.X());
      rect.SetWidth(line_rect.Width());
    }
    return rect;
  }
  // FragmentItem::CaretInlinePositionForOffset() rounds to a LayoutUnit.
  LayoutUnit caret_left;
  if (caret.position_type != InlineCaretPositionType::kEmptyLine)
    caret_left = LayoutUnit::FromFloatRound(item.CaretInlinePosition(caret.text_offset, fragments_->TextContent()));
  if (caret.position_type == InlineCaretPositionType::kAtTextOffset && !item.IsLineBreak()) caret_left -= caret_width / 2;
  // Text carets use the text fragment's block size and offset, not line-height.
  PhysicalRect rect = InlineRangeRect(item, caret_left, caret_left + caret_width, options_.writing_mode);

  // ComputeLocalCaretRectAtTextOffset(): adjust the location to ensure that
  // it completely falls in the union of line box and containing block, and
  // then round it to the nearest pixel.
  const PhysicalRect& line_box_rect = line.RectInContainerFragment();
  const PhysicalSize fragment_size = fragments_->SizeInPhysicalCoordinates();
  if (IsHorizontalWritingMode(options_.writing_mode)) {
    // ShouldAlignCaretRight() for text-align: start.
    if (IsRtl(line.ResolvedDirection())) {
      const LayoutUnit left_edge = std::min(LayoutUnit(), line_box_rect.X());
      const LayoutUnit right_limit = line_box_rect.Right() - caret_width;
      rect.offset.left = ClampAndRound(rect.offset.left, left_edge, right_limit);
    } else {
      const LayoutUnit right_limit = std::max(fragment_size.width, line_box_rect.Right()) - caret_width;
      rect.offset.left = ClampAndRound(rect.offset.left, line_box_rect.X(), right_limit);
    }
    return rect;
  }

  // Similar adjustment and rounding for vertical text.
  const LayoutUnit min_y = std::min(LayoutUnit(), line_box_rect.Y());
  const LayoutUnit bottom_limit = std::max(fragment_size.height, line_box_rect.Bottom()) - caret_width;
  rect.offset.top = ClampAndRound(rect.offset.top, min_y, bottom_limit);
  return rect;
}

InlinePosition InlineFormattingContext::HitTest(const PhysicalOffset& point) {
  const auto& fragments = Fragments();
  const bool horizontal = IsHorizontalWritingMode(options_.writing_mode);
  size_t nearest_line = 0;
  float distance = std::numeric_limits<float>::infinity();
  for (size_t index : fragments.Lines()) {
    const auto& rect = fragments[index].RectInContainerFragment();
    const float candidate = horizontal ? AxisDistance(point.top.ToFloat(), rect.Y(), rect.Bottom()) : AxisDistance(point.left.ToFloat(), rect.X(), rect.Right());
    if (candidate < distance) {
      distance = candidate;
      nearest_line = index;
    }
  }
  const auto& line = fragments[nearest_line];
  if (line.DescendantsCount() == 1) {
    auto position = fragments.Mapping().GetPosition(line.TextOffset().start);
    return position ? position : InlinePosition{root_->Id(), 0};
  }
  const FragmentItem* nearest = nullptr;
  distance = std::numeric_limits<float>::infinity();
  for (size_t i = nearest_line + 1; i < nearest_line + line.DescendantsCount(); ++i) {
    const auto& item = fragments[i];
    const auto& rect = item.RectInContainerFragment();
    const float candidate = horizontal ? AxisDistance(point.left.ToFloat(), rect.X(), rect.Right()) : AxisDistance(point.top.ToFloat(), rect.Y(), rect.Bottom());
    if (candidate < distance) {
      distance = candidate;
      nearest = &item;
    }
  }
  const auto& rect = nearest->RectInContainerFragment();
  const float advance = horizontal ? (point.left - rect.X()).ToFloat() : options_.writing_mode == WritingMode::kSidewaysLr ? (rect.Bottom() - point.top).ToFloat()
                                                                                                                           : (point.top - rect.Y()).ToFloat();
  unsigned offset = nearest->TextOffsetForPoint(advance, fragments.TextContent());
  if (nearest->IsGeneratedText()) {
    const auto* unit = fragments.Mapping().GetUnit(nearest->GetLayoutObject()->Id());
    return {unit->object->Id(), offset - unit->start, TextAffinity::kUpstream};
  }
  if (nearest->IsLineBreak()) offset = nearest->TextOffset().start;
  InlineCursor cursor(*this);
  cursor.MoveToItem(static_cast<size_t>(nearest - fragments.Items().data()));
  const auto adjusted = AdjustHitTestForBidi({cursor, nearest->IsAtomicInline() ? (offset == nearest->TextOffset().start ? InlineCaretPositionType::kBeforeBox : InlineCaretPositionType::kAfterBox) : InlineCaretPositionType::kAtTextOffset, offset});
  nearest = adjusted.cursor.Current();
  offset = adjusted.text_offset;
  const auto affinity = offset == nearest->TextOffset().end ? TextAffinity::kUpstream : TextAffinity::kDownstream;
  const auto* unit = fragments.Mapping().GetUnit(nearest->GetLayoutObject()->Id());
  return {unit->object->Id(), offset - unit->start, affinity};
}

Vector<PhysicalRect> InlineFormattingContext::SelectionRects(const InlineSelection& selection) {
  return CollectSelectionRects(selection, nullptr);
}

Vector<PhysicalRect> InlineFormattingContext::SelectionRectsForPaint(const InlineSelection& selection,
                                                                    const PhysicalOffset& paint_offset) {
  return CollectSelectionRects(selection, &paint_offset);
}

Vector<PhysicalRect> InlineFormattingContext::CollectSelectionRects(const InlineSelection& selection,
                                                                   const PhysicalOffset* paint_offset) {
  const auto& fragments = Fragments();
  const auto anchor = fragments.Mapping().GetTextContentOffset(selection.anchor);
  const auto focus = fragments.Mapping().GetTextContentOffset(selection.focus);
  Vector<PhysicalRect> rects;
  if (!anchor || !focus || *anchor == *focus) return rects;
  const unsigned start = std::min(*anchor, *focus);
  const unsigned end = std::max(*anchor, *focus);
  const auto append_rect = [&](const FragmentItem& item, const FragmentItem& line, PhysicalRect rect) {
    rect = ExpandSelectionRectToLineHeight(rect, line, options_.writing_mode);
    if (paint_offset && item.IsText()) {
      // HighlightPainter::SelectionPaintState::ComputeSelectionRectIfNeeded
      // adds PhysicalBoxRect's origin, including its line-relative y rounding.
      const auto& item_rect = item.RectInContainerFragment();
      const auto box = PhysicalBoxRect(item_rect, *paint_offset, line.RectInContainerFragment().offset, nullptr);
      rect.Move(box.offset - item_rect.offset);
    } else if (paint_offset) {
      rect.Move(*paint_offset);
    }
    rects.push_back(rect);
  };
  for (const auto& item : fragments.Items()) {
    if (item.Type() == FragmentItem::kLine) continue;
    const auto& line = fragments[item.LineIndex()];
    const unsigned from = std::max(start, item.TextOffset().start);
    const unsigned to = std::min(end, item.TextOffset().end);
    if (item.IsGeneratedText() && start < item.TextOffset().start && end >= item.TextOffset().start) {
      append_rect(item, line, item.RectInContainerFragment());
      continue;
    }
    // MoveToLastLogicalLeaf uses the visual last/first leaf for LTR/RTL.
    const size_t last_leaf_index = item.LineIndex() +
                                  (IsLtr(line.ResolvedDirection()) ? line.DescendantsCount() - 1 : 1);
    const bool selected_soft_wrap = item.IsText() && !item.IsGeneratedText() && !item.IsLineBreak() &&
                                    line.HasSoftWrapToNextLine() && &item == &fragments[last_leaf_index] &&
                                    start <= item.TextOffset().end && end > item.TextOffset().end &&
                                    item.ResolvedDirection() == line.ResolvedDirection();
    if (item.IsGeneratedText() || from > to || (from == to && !selected_soft_wrap)) continue;
    LayoutUnit start_position;
    LayoutUnit end_position = item.InlineSize();
    // FragmentItem::LocalRect() covers the whole item without measuring.
    if (item.IsLineBreak()) {
      // CurrentLocalSelectionRectForText(): preserved newlines get a space
      // width. The last-<br> exception does not apply to this text-only model.
      end_position = LayoutUnit(item.Style().font.SpaceWidth());
      if (IsRtl(item.ResolvedDirection())) {
        start_position = -end_position;
        end_position = LayoutUnit();
      }
    } else if (from != item.TextOffset().start || to != item.TextOffset().end) {
      // LineLeftAndRightForOffsets expands partial grapheme selections at
      // both ends, before rounding outwards to LayoutUnit precision.
      const float begin = item.CaretInlinePosition(from, fragments.TextContent(), AdjustMidCluster::kToStart);
      const float finish = item.CaretInlinePosition(to, fragments.TextContent(), AdjustMidCluster::kToEnd);
      const auto edges = LineLeftAndRightForPositions(begin, finish);
      start_position = edges.first;
      end_position = edges.second;
    }
    // IsBeforeSoftLineBreak/ExpandedSelectionRectForSoftLineBreakIfNeeded:
    // extend the last logical text leaf only when the selection crosses the
    // wrap and its direction agrees with the line's base direction.
    if (selected_soft_wrap) {
      const LayoutUnit space_width(item.Style().font.SpaceWidth());
      if (IsLtr(item.ResolvedDirection()))
        end_position += space_width;
      else
        start_position -= space_width;
    }
    append_rect(item, line, InlineRangeRect(item, start_position, end_position, options_.writing_mode));
  }
  return rects;
}

Vector<PhysicalRect> InlineFormattingContext::ObjectRects(const InlineObject& object) {
  Vector<PhysicalRect> rects;
  InlineCursor cursor(*this);
  cursor.MoveToIncludingCulledInline(object);
  for (; cursor; cursor.MoveToNextForSameLayoutObject()) rects.push_back(cursor.Current()->RectInContainerFragment());
  return rects;
}

void InlineFormattingContext::Paint(PaintCanvas* canvas, const PhysicalOffset& offset) {
  if (!canvas) return;
  const auto& fragments = Fragments();
  for (const auto& item : fragments.Items()) {
    if (!item.IsText() || item.IsLineBreak() || !item.TextShapeResult()) continue;
    // BoxFragmentPainter::PaintLineBoxChildItems passes the line box offset
    // as the parent offset of its children.
    const PhysicalRect box = PhysicalBoxRect(item.RectInContainerFragment(), offset,
                                             fragments[item.LineIndex()].RectInContainerFragment().offset, nullptr);
    const auto range = item.TextOffset();
    const TextFragmentPaintInfo info{item.IsGeneratedText() ? StringView(item.GeneratedText()) : StringView(fragments.TextContent()),
                                     item.IsGeneratedText() ? 0 : range.start, item.IsGeneratedText() ? item.GeneratedText().length() : range.end, item.TextShapeResult()};
    PaintTextFragment(canvas, info, item.Style().font, options_.writing_mode, box, item.Style().paint);
  }
}

} // namespace bkfont
