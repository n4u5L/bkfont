// Ported from: blink/renderer/core/layout/inline/inline_cursor.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "layout/inline/inline_formatting_context.h"

namespace bkfont {

// Temporary traversal. A stale cursor becomes null, including after root death.
// It never owns fragments or keeps removed model objects alive.
class InlineCursor {
public:
  InlineCursor() = default;
  explicit InlineCursor(InlineFormattingContext&);
  const FragmentItem* Current() const;
  explicit operator bool() const {
    return Current() != nullptr;
  }
  bool IsNull() const {
    return !Current();
  }
  void MoveTo(const InlineObject&);
  void MoveToIncludingCulledInline(const InlineObject&);
  void MoveToNext();
  void MoveToPrevious();
  void MoveToNextSkippingChildren();
  void MoveToNextForSameLayoutObject();
  void MoveToLastForSameLayoutObject();
  void MoveToVisualFirstForSameLayoutObject();
  void MoveToVisualLastForSameLayoutObject();
  void MoveToContainingLine();
  void MoveToNextLine();
  void MoveToPreviousLine();
  InlineCursor CursorForDescendants() const;
  InlineCursor CursorForRoot() const;
  size_t ItemIndex() const {
    return index_;
  }
  void MoveToItem(size_t);

private:
  bool IsValid() const;
  InlineFormattingContext* root_ = nullptr;
  const FragmentItems* items_ = nullptr;
  std::weak_ptr<InlineLayoutEpoch> epoch_;
  uint64_t generation_ = 0;
  size_t begin_ = 0;
  size_t end_ = 0;
  size_t index_ = 0;
  const InlineObject* culled_inline_ = nullptr;
};

} // namespace bkfont
