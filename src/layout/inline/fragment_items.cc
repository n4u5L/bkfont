// Ported from: blink/renderer/core/layout/inline/fragment_items.cc
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "fragment_items.h"

#include <algorithm>
#include <cassert>

namespace bkit {

size_t FragmentItems::FirstInlineFragmentItemIndex(const InlineObject& object) const {
  const auto found = first_items_.find(&object);
  return found == first_items_.end() ? 0 : found->value;
}

void FragmentItems::FinalizeAfterLayout() {
  struct LastItem {
    size_t index;
    size_t fragment_id;
  };
  HashMap<const InlineObject*, LastItem> last_items;
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
    const auto result = last_items.insert(item.object_, LastItem{index, 0});
    if (result.is_new_entry) {
      item.fragment_id_ = 0;
      first_items_.insert(item.object_, index + 1);
      continue;
    }
    auto* const last = result.stored_value;
    FragmentItem& previous = items_[last->value.index];
    previous.delta_to_next_ = index - last->value.index;
    previous.is_last_for_node_ = false;
    item.fragment_id_ = ++last->value.fragment_id;
    last->value.index = index;
  }
}

void FragmentItems::DirtyLine(size_t index) {
  items_[items_[index].line_index_].dirty_ = true;
}

void FragmentItems::DirtyFirstItem() {
  if (!items_.empty()) items_.front().dirty_ = true;
}

void FragmentItems::RefreshStyle(const InlineObject& object, const std::shared_ptr<const ComputedStyle>& style) {
  // Line items have no LayoutObject pointer and use the root's style.
  if (!object.Parent()) {
    for (const size_t index : lines_) items_[index].style_ = style;
  }
  size_t index = FirstInlineFragmentItemIndex(object);
  if (!index) return;
  --index;
  for (;;) {
    auto& item = items_[index];
    item.style_ = style;
    if (!item.delta_to_next_) break;
    index += item.delta_to_next_;
  }
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
  const auto mapped = mapping_->GetTextContentOffset({object.Id(), offset});
  if (!mapped) {
    DirtyLinesFromChangedChild(object);
    return;
  }
  const unsigned text_offset = *mapped;
  size_t dirty = items_.size();
  if (!object.IsInline()) {
    // InlineCursor::MoveTo()/MoveToNextForSameLayoutObject(): this chain has
    // the same visual order as items_, including generated hyphens. A local
    // text edit need not inspect every other object's fragments first.
    if (size_t index = FirstInlineFragmentItemIndex(object)) {
      --index;
      for (;;) {
        const auto& item = items_[index];
        if (item.text_offset_.end >= text_offset) {
          dirty = item.line_index_;
          break;
        }
        if (!item.delta_to_next_) break;
        index += item.delta_to_next_;
      }
    }
  } else {
    // Culled inline containers have no own chain; include their descendants.
    for (size_t i = 0; i < items_.size(); ++i) {
      const auto& item = items_[i];
      if (item.object_ && (item.object_ == &object || item.object_->IsDescendantOf(object)) &&
          item.text_offset_.end >= text_offset) {
        dirty = item.line_index_;
        break;
      }
    }
  }
  if (dirty == items_.size()) {
    DirtyLinesFromChangedChild(object);
    return;
  }
  // Reconsider the previous line: a text edit or whitespace style change at
  // an inline boundary may pull the next word onto it.
  const auto line = std::lower_bound(lines_.begin(), lines_.end(), dirty);
  if (line != lines_.begin()) dirty = *std::prev(line);
  DirtyLine(dirty);
}

} // namespace bkit
