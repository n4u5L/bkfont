// Ported from: blink/renderer/core/layout/inline/inline_cursor.cc
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_cursor.h"

#include <algorithm>
#include <cassert>

#include "editing/bidi_adjustment.h"
#include "layout/geometry/writing_mode_converter.h"
#include "layout/inline/inline_caret_position.h"
#include "runtime_enabled_features.h"

namespace bkit {
namespace {

bool ShouldIgnoreForPositionForPoint(const FragmentItem& item) {
  // Non-atomic inline boxes are culled in this model. There are no floats,
  // pseudo nodes, block-in-inline boxes or fragments hidden by ellipsis.
  // CSS visibility is not an exclusion in Blink's PositionForPoint path.
  return item.IsGeneratedText() || (!item.IsText() && !item.IsAtomicInline());
}

bool ShouldIgnoreForPositionForPoint(const InlineCursor& line) {
  for (auto cursor = line.CursorForDescendants(); cursor; cursor.MoveToNext()) {
    if (!ShouldIgnoreForPositionForPoint(*cursor.Current())) return false;
  }
  return true;
}

} // namespace

InlineCursor::InlineCursor(InlineFormattingContext& root)
    : root_(&root),
      items_(&root.Fragments()),
      epoch_(root.epoch_),
      generation_(root.Generation()),
      end_(items_->Size()) {
}

bool InlineCursor::IsValid() const {
  const auto epoch = epoch_.lock();
  return epoch && epoch->state == InlineLayoutState::kClean && epoch->generation == generation_;
}

const FragmentItem* InlineCursor::Current() const {
  return IsValid() && index_ >= begin_ && index_ < end_ ? &(*items_)[index_] : nullptr;
}

void InlineCursor::MoveTo(const InlineObject& object) {
  if (!root_) *this = InlineCursor(object.Root());
  culled_inline_ = nullptr;
  if (!IsValid() || &object.Root() != root_) {
    index_ = end_;
    return;
  }
  size_t index = items_->FirstInlineFragmentItemIndex(object);
  if (!index) {
    index_ = end_;
    return;
  }
  --index;
  while (index < begin_) {
    const size_t delta = (*items_)[index].DeltaToNextForSameLayoutObject();
    if (!delta) {
      index_ = end_;
      return;
    }
    index += delta;
  }
  index_ = std::min(index, end_);
}

void InlineCursor::MoveToIncludingCulledInline(const InlineObject& object) {
  MoveTo(object);
  if (Current() || !IsValid() || !object.IsInline() || &object.Root() != root_) return;
  culled_inline_ = &object;
  for (index_ = begin_; index_ < end_; ++index_) {
    const auto* descendant = (*items_)[index_].GetLayoutObject();
    if (descendant && descendant->IsDescendantOf(object)) return;
  }
}

void InlineCursor::MoveToNext() {
  if (Current()) ++index_;
}
void InlineCursor::MoveToPrevious() {
  if (Current()) index_ = index_ > begin_ ? index_ - 1 : end_;
}
void InlineCursor::MoveToNextSkippingChildren() {
  if (const auto* item = Current()) index_ = std::min(end_, index_ + item->DescendantsCount());
}
void InlineCursor::MoveToNextForSameLayoutObject() {
  if (!Current()) return;
  if (culled_inline_) {
    while (++index_ < end_) {
      const auto* object = (*items_)[index_].GetLayoutObject();
      if (object && object->IsDescendantOf(*culled_inline_)) return;
    }
    return;
  }
  const size_t delta = Current()->DeltaToNextForSameLayoutObject();
  index_ = delta ? std::min(index_ + delta, end_) : end_;
}
void InlineCursor::MoveToLastForSameLayoutObject() {
  InlineCursor next = *this;
  while (next) {
    *this = next;
    next.MoveToNextForSameLayoutObject();
  }
}
void InlineCursor::MoveToVisualFirstForSameLayoutObject() {
  if (!Current()) return;
  if (culled_inline_)
    MoveToIncludingCulledInline(*culled_inline_);
  else if (const auto* object = Current()->GetLayoutObject())
    MoveTo(*object);
}
void InlineCursor::MoveToVisualLastForSameLayoutObject() {
  MoveToLastForSameLayoutObject();
}
void InlineCursor::MoveToContainingLine() {
  if (const auto* item = Current()) MoveToItem(item->LineIndex());
}
void InlineCursor::MoveToNextLine() {
  if (!Current()) return;
  const auto lines = items_->Lines();
  const auto found = std::upper_bound(lines.begin(), lines.end(), Current()->LineIndex());
  MoveToItem(found != lines.end() ? *found : end_);
}
void InlineCursor::MoveToPreviousLine() {
  if (!Current()) return;
  const auto lines = items_->Lines();
  const auto found = std::lower_bound(lines.begin(), lines.end(), Current()->LineIndex());
  MoveToItem(found != lines.begin() ? *std::prev(found) : end_);
}

void InlineCursor::MoveToFirstLogicalLeaf() {
  if (!Current()) return;
  assert(Current()->Type() == FragmentItem::kLine);
  // Match Blink's visual-edge choice, including its mixed-bidi limitation.
  const auto descendants = CursorForDescendants();
  MoveToItem(descendants ? (IsLtr(Current()->ResolvedDirection()) ? descendants.begin_ : descendants.end_ - 1) : end_);
}

void InlineCursor::MoveToLastLogicalLeaf() {
  if (!Current()) return;
  assert(Current()->Type() == FragmentItem::kLine);
  const auto descendants = CursorForDescendants();
  MoveToItem(descendants ? (IsLtr(Current()->ResolvedDirection()) ? descendants.end_ - 1 : descendants.begin_) : end_);
}

void InlineCursor::MoveToFirstNonPseudoLeaf() {
  for (; *this; MoveToNext()) {
    if (ShouldIgnoreForPositionForPoint(*Current())) continue;
    if (Current()->IsLineBreak()) {
      auto next = *this;
      next.MoveToNext();
      if (next) continue;
    }
    return;
  }
}

void InlineCursor::MoveToLastNonPseudoLeaf() {
  InlineCursor last_leaf;
  for (auto cursor = *this; cursor; cursor.MoveToNext()) {
    const auto& item = *cursor.Current();
    if (item.Type() == FragmentItem::kLine) continue;
    if (item.IsLineBreak() && last_leaf) break;
    if (item.IsGeneratedText()) break;
    if (item.IsText()) {
      const auto range = item.TextOffset();
      if (items_->Mapping().HasBidiControlCharactersOnly(range.start, range.end)) continue;
      last_leaf = cursor;
    } else if (item.IsAtomicInline()) {
      last_leaf = cursor;
    }
  }
  *this = last_leaf;
}
InlineCursor InlineCursor::CursorForDescendants() const {
  InlineCursor result = *this;
  if (const auto* item = Current()) {
    result.begin_ = index_ + 1;
    result.end_ = std::min(end_, index_ + item->DescendantsCount());
    result.index_ = result.begin_;
    result.culled_inline_ = nullptr;
  }
  return result;
}
void InlineCursor::MoveToItem(size_t index) {
  index_ = index >= begin_ && index < end_ ? index : end_;
}

InlineCursor InlineCursor::CursorForRoot() const {
  InlineCursor result = *this;
  if (IsValid()) {
    result.begin_ = 0;
    result.end_ = items_->Size();
  }
  return result;
}

InlinePosition InlineCursor::PositionForPointInInlineFormattingContext(const PhysicalOffset& point,
                                                                       InlineHitTestOptions options) {
  if (!Current()) return {};
  const WritingModeConverter converter(items_->GetWritingMode(), items_->Direction(),
                                        items_->SizeInPhysicalCoordinates());
  const LayoutUnit point_block_offset = converter.ToLogical(point, {LayoutUnit(1), LayoutUnit(1)}).block_offset;
  InlineCursor closest_line_after;
  LayoutUnit closest_line_after_block_offset = LayoutUnit::Min();
  InlineCursor closest_line_before;
  LayoutUnit closest_line_before_block_offset = LayoutUnit::Max();

  for (; *this; MoveToNextSkippingChildren()) {
    const auto& item = *Current();
    if (item.Type() != FragmentItem::kLine || ShouldIgnoreForPositionForPoint(*this)) continue;
    const auto& rect = item.RectInContainerFragment();
    const LayoutUnit child_block_offset = converter.ToLogical(rect.offset, rect.size).block_offset;
    if (point_block_offset < child_block_offset) {
      if (child_block_offset < closest_line_before_block_offset) {
        closest_line_before_block_offset = child_block_offset;
        closest_line_before = *this;
      }
      continue;
    }
    const LayoutUnit child_block_end_offset = child_block_offset + converter.ToLogical(rect.size).block_size;
    // Hitting the block-end edge doesn't count, even when lines touch.
    if (point_block_offset >= child_block_end_offset) {
      if (child_block_end_offset > closest_line_after_block_offset) {
        closest_line_after_block_offset = child_block_end_offset;
        closest_line_after = *this;
      }
      continue;
    }
    if (const auto position = PositionForPointInInlineBox(point)) return position;
  }

  // Preserve upstream's candidate order; this is not a distance comparison.
  if (closest_line_before) {
    *this = closest_line_before;
    if (options.move_caret_to_horizontal_boundary_when_past_top_or_bottom) {
      if (auto position = PositionForStartOfLine()) {
        position.affinity = TextAffinity::kDownstream;
        return position;
      }
    } else if (const auto position = PositionForPointInInlineBox(point)) {
      return position;
    }
  }
  if (closest_line_after) {
    *this = closest_line_after;
    if (options.move_caret_to_horizontal_boundary_when_past_top_or_bottom) {
      if (auto position = PositionForEndOfLine()) {
        position.affinity = TextAffinity::kDownstream;
        return position;
      }
    } else if (const auto position = PositionForPointInInlineBox(point)) {
      return position;
    }
  }
  return {};
}

InlinePosition InlineCursor::PositionForPointInInlineBox(const PhysicalOffset& point) const {
  if (!Current()) return {};
  assert(Current()->Type() == FragmentItem::kLine);
  const WritingModeConverter converter(items_->GetWritingMode(), items_->Direction(),
                                        Current()->RectInContainerFragment().size);
  const LayoutUnit point_inline_offset = converter.ToLogical(point, {LayoutUnit(1), LayoutUnit(1)}).inline_offset;
  InlineCursor closest_child_before;
  LayoutUnit closest_child_before_inline_offset = LayoutUnit::Min();
  InlineCursor closest_child_after;
  LayoutUnit closest_child_after_inline_offset = LayoutUnit::Max();

  for (auto child = CursorForDescendants(); child; child.MoveToNext()) {
    const auto& item = *child.Current();
    if (ShouldIgnoreForPositionForPoint(item)) continue;
    const auto& rect = item.RectInContainerFragment();
    const LayoutUnit child_inline_offset = converter.ToLogical(rect.offset, rect.size).inline_offset;
    if (point_inline_offset < child_inline_offset) {
      if (child_inline_offset < closest_child_after_inline_offset) {
        closest_child_after_inline_offset = child_inline_offset;
        closest_child_after = child;
      }
      continue;
    }
    const LayoutUnit child_inline_end_offset = child_inline_offset + converter.ToLogical(rect.size).inline_size;
    if (point_inline_offset >= child_inline_end_offset) {
      if (child_inline_end_offset > closest_child_before_inline_offset) {
        closest_child_before_inline_offset = child_inline_end_offset;
        closest_child_before = child;
      }
      continue;
    }
    if (const auto position = child.PositionForPointInChild(point)) return position;
  }
  if (closest_child_after) {
    if (const auto position = closest_child_after.PositionForPointInChild(point)) return position;
  }
  if (closest_child_before) return closest_child_before.PositionForPointInChild(point);
  return {};
}

InlinePosition InlineCursor::PositionForPointInChild(const PhysicalOffset& point) const {
  if (!Current() || ShouldIgnoreForPositionForPoint(*Current())) return {};
  const auto& item = *Current();
  const auto& rect = item.RectInContainerFragment();
  const auto writing_mode = items_->GetWritingMode();
  if (item.IsText()) {
    // TextOffsetForPoint uses a zero-size point and LTR line coordinates,
    // independently of the resolved bidi direction of this text fragment.
    const WritingModeConverter converter(writing_mode, TextDirection::kLtr, rect.size);
    const LayoutUnit advance = converter.ToLogical(point - rect.offset, {}).inline_offset;
    return PositionForPointInText(item.TextOffsetForPoint(advance.ToFloat(), items_->TextContent()));
  }

  // The standalone atomic object is a replaced leaf, not an inline-block
  // containing another formatting context. Match LayoutReplaced here.
  const WritingModeConverter converter(writing_mode, items_->Direction(), items_->SizeInPhysicalCoordinates());
  auto line = CursorForRoot();
  line.MoveToContainingLine();
  const auto& line_rect = line.Current()->RectInContainerFragment();
  const LayoutUnit top = converter.ToLogical(line_rect.offset, line_rect.size).block_offset;
  const LayoutUnit bottom = top + converter.ToLogical(line_rect.size).block_size;
  const auto logical_point = converter.ToLogical(point, {});
  if (logical_point.block_offset < top || logical_point.block_offset >= bottom)
    return {item.GetLayoutObject()->Id(), 0};
  const auto logical_offset = converter.ToLogical(rect.offset, rect.size);
  const bool at_left_side = logical_point.inline_offset <= logical_offset.inline_offset + item.InlineSize() / 2;
  const bool at_start = at_left_side == IsLtr(item.ResolvedDirection());
  return {item.GetLayoutObject()->Id(), at_start ? 0u : 1u};
}

InlinePosition InlineCursor::PositionForPointInText(unsigned text_offset) const {
  if (!Current() || !Current()->IsText() || Current()->IsGeneratedText()) return {};
  InlineCaretPosition position{*this, InlineCaretPositionType::kAtTextOffset, text_offset};
  if (!RuntimeEnabledFeatures::BidiCaretAffinityEnabled()) position = AdjustHitTestForBidi(position);
  return position.ToPositionInDOMTreeWithAffinity(items_->Mapping());
}

InlinePosition InlineCursor::PositionForStartOfLine() const {
  if (!Current()) return {};
  assert(Current()->Type() == FragmentItem::kLine);
  auto first_leaf = CursorForDescendants();
  if (IsLtr(Current()->ResolvedDirection())) first_leaf.MoveToFirstNonPseudoLeaf();
  else first_leaf.MoveToLastNonPseudoLeaf();
  if (!first_leaf) return {};
  const auto& item = *first_leaf.Current();
  if (!item.IsText()) return {item.GetLayoutObject()->Id(), 0};
  const unsigned offset = Current()->ResolvedDirection() == item.ResolvedDirection() ? item.TextOffset().start : item.TextOffset().end;
  return first_leaf.PositionForPointInText(offset);
}

InlinePosition InlineCursor::PositionForEndOfLine() const {
  if (!Current()) return {};
  assert(Current()->Type() == FragmentItem::kLine);
  auto last_leaf = CursorForDescendants();
  if (IsLtr(Current()->ResolvedDirection())) last_leaf.MoveToLastNonPseudoLeaf();
  else last_leaf.MoveToFirstNonPseudoLeaf();
  if (!last_leaf) return {};
  const auto& item = *last_leaf.Current();
  if (!item.IsText()) return {item.GetLayoutObject()->Id(), 1};
  const unsigned offset = Current()->ResolvedDirection() == item.ResolvedDirection() && !item.IsLineBreak()
                              ? item.TextOffset().end : item.TextOffset().start;
  return last_leaf.PositionForPointInText(offset);
}

} // namespace bkit
