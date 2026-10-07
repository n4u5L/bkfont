// Ported from: blink/renderer/core/layout/inline/fragment_item.cc
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "fragment_item.h"

#include <algorithm>

namespace bkit {

float FragmentItem::CaretInlinePosition(unsigned offset, const String& text,
                                        AdjustMidCluster adjust_mid_cluster) const {
  if (IsGeneratedText()) return 0;
  offset = std::clamp(offset, text_offset_.start, text_offset_.end);
  if (!shape_ || !shape_->NumCharacters()) {
    const bool at_end = offset == text_offset_.end;
    return at_end == IsLtr(ResolvedDirection()) ? inline_size_.ToFloat() : 0.0f;
  }
  if (!caret_shape_) caret_shape_ = shape_->CreateShapeResult();
  // ShapeResult offsets are relative to StartIndex; its grapheme input must
  // cover exactly that result, including ligatures split across text nodes.
  return caret_shape_->CaretPositionForOffset(offset - shape_->StartIndex(),
                                              StringView(text, shape_->StartIndex(), shape_->NumCharacters()),
                                              adjust_mid_cluster);
}

unsigned FragmentItem::TextOffsetForPoint(float position, const String& text) const {
  if (IsGeneratedText()) return text_offset_.start;
  if (!shape_ || !shape_->NumCharacters()) {
    // Zero-width flow controls always resolve to their start. Atomic boxes
    // use their own PositionForPoint path, including its midpoint rule.
    if (!inline_size_) return text_offset_.start;
    const float inline_offset = IsLtr(ResolvedDirection()) ? position : inline_size_.ToFloat() - position;
    return inline_offset <= inline_size_.ToFloat() / 2 ? text_offset_.start : text_offset_.end;
  }
  if (!caret_shape_) caret_shape_ = shape_->CreateShapeResult();
  const unsigned offset = shape_->StartIndex() + caret_shape_->CaretOffsetForHitTest(position,
                                                                                     StringView(text, shape_->StartIndex(), shape_->NumCharacters()));
  return std::clamp(offset, text_offset_.start, text_offset_.end);
}

} // namespace bkit
