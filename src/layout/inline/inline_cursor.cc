// Ported from: blink/renderer/core/layout/inline/inline_cursor.cc
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_cursor.h"

#include <algorithm>

namespace bkfont {

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

} // namespace bkfont
