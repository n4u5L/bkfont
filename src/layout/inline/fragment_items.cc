// Ported from: blink/renderer/core/layout/inline/fragment_items.cc
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "fragment_items.h"

#include <algorithm>
#include <cassert>

namespace bkfont {

size_t FragmentItems::FirstInlineFragmentItemIndex(const InlineObject& object) const {
  const auto found = first_items_.find(&object);
  return found == first_items_.end() ? 0 : found->second;
}

void FragmentItems::FinalizeAfterLayout() {
  struct LastItem {
    size_t index;
    size_t fragment_id;
  };
  std::unordered_map<const InlineObject*, LastItem> last_items;
  first_items_.clear();
  lines_.clear();
  size_t line_fragment_id = FragmentItem::kInitialLineFragmentId;
  size_t line_index = 0;
  for (size_t index = 0; index < items_.size(); ++index) {
    FragmentItem& item = items_[index];
    item.delta_to_next_ = 0;
    item.dirty_ = false;
    if (item.Type() == FragmentItem::kLine) {
      item.fragment_id_ = line_fragment_id++;
      line_index = index;
      item.line_index_ = index;
      lines_.push_back(index);
      continue;
    }
    // Reused lines must never retain a detached/retired object.
    assert(item.object_ && item.object_->IsAttached());
    item.line_index_ = line_index;
    item.is_last_for_node_ = true;
    const auto [last, is_first] = last_items.emplace(item.object_, LastItem{index, 0});
    if (is_first) {
      item.fragment_id_ = 0;
      first_items_.emplace(item.object_, index + 1);
      continue;
    }
    FragmentItem& previous = items_[last->second.index];
    previous.delta_to_next_ = index - last->second.index;
    previous.is_last_for_node_ = false;
    item.fragment_id_ = ++last->second.fragment_id;
    last->second.index = index;
  }
}

void FragmentItems::DirtyLine(size_t index) {
  items_[items_[index].line_index_].dirty_ = true;
}

void FragmentItems::DirtyFirstItem() {
  if (!items_.empty()) items_.front().dirty_ = true;
}

bool FragmentItems::TryDirtyFirstLineFor(const InlineObject& object) {
  const size_t first = FirstInlineFragmentItemIndex(object);
  if (!first) return false;
  DirtyLine(first - 1);
  return true;
}

bool FragmentItems::TryDirtyLastLineFor(const InlineObject& object) {
  size_t index = FirstInlineFragmentItemIndex(object);
  if (!index) return false;
  --index;
  while (const size_t delta = items_[index].delta_to_next_) index += delta;
  DirtyLine(index);
  return true;
}

void FragmentItems::DirtyLinesFromChangedChild(const InlineObject& child) {
  if (TryDirtyFirstLineFor(child)) return;
  // Culled inlines have no own item. Their first descendant can be on an
  // earlier line than their previous sibling's last fragment after bidi.
  if (child.IsInline()) {
    for (size_t i = 0; i < items_.size(); ++i) {
      if (items_[i].object_ && items_[i].object_->IsDescendantOf(child)) {
        DirtyLine(i);
        return;
      }
    }
  }
  for (const InlineObject* current = &child;;) {
    if (const InlineObject* previous = current->PreviousSibling()) {
      while (previous->IsInline() && previous->LastChild()) previous = previous->LastChild();
      current = previous;
      if (TryDirtyLastLineFor(*current)) return;
      continue;
    }
    current = current->Parent();
    if (!current || !current->Parent()) {
      DirtyFirstItem();
      return;
    }
    if (TryDirtyFirstLineFor(*current)) return;
  }
}

void FragmentItems::DirtyTextRange(const InlineObject& object, unsigned offset) {
  const auto* unit = mapping_.GetUnit(object.Id());
  if (!unit) {
    DirtyLinesFromChangedChild(object);
    return;
  }
  const unsigned text_offset = unit->start + offset;
  size_t dirty = items_.size();
  for (size_t i = 0; i < items_.size(); ++i) {
    const auto& item = items_[i];
    if (item.object_ == &object && item.text_offset_.end >= text_offset) {
      dirty = item.line_index_;
      break;
    }
  }
  if (dirty == items_.size()) {
    DirtyLinesFromChangedChild(object);
    return;
  }
  // Reconsider the previous line: deletion may pull the next word onto it.
  const auto line = std::lower_bound(lines_.begin(), lines_.end(), dirty);
  if (line != lines_.begin()) dirty = *std::prev(line);
  DirtyLine(dirty);
}

} // namespace bkfont
