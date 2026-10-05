// Ported from: blink/renderer/core/layout/inline/fragment_item.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <memory>

#include "layout/geometry/physical_rect.h"
#include "layout/inline/inline_object.h"
#include "shaping/shape_result_view.h"
#include "text/text_direction.h"

namespace bkfont {

class TextCombine;

struct TextOffsetRange {
  unsigned start = 0;
  unsigned end = 0;
  unsigned Length() const {
    return end - start;
  }
  bool operator==(const TextOffsetRange&) const = default;
};

// Flat preorder storage: a line is followed by its visual-order leaves.
// Non-atomic inline containers are culled; the cursor also visits their leaves.
class FragmentItem {
public:
  enum ItemType {
    kInvalid,
    kText,
    kLine,
    kBox,
    kGeneratedText
  };
  static constexpr size_t kInitialLineFragmentId = 0x80000000;
  ItemType Type() const {
    return type_;
  }
  bool IsText() const {
    return type_ == kText || type_ == kGeneratedText;
  }
  bool IsGeneratedText() const {
    return type_ == kGeneratedText;
  }
  const String& GeneratedText() const {
    return generated_text_;
  }
  bool IsAtomicInline() const {
    return type_ == kBox;
  }
  bool IsLineBreak() const {
    return line_break_;
  }
  bool IsLastForNode() const {
    return is_last_for_node_;
  }
  bool IsFirstForNode() const {
    return fragment_id_ == 0;
  }
  bool IsDirty() const {
    return dirty_;
  }
  bool HasSoftWrapToNextLine() const {
    return soft_wrap_;
  }
  const InlineObject* GetLayoutObject() const {
    return object_;
  }
  const ComputedStyle& Style() const {
    return *style_;
  }
  std::shared_ptr<const ComputedStyle> StyleSnapshot() const { return style_; }
  const PhysicalRect& RectInContainerFragment() const {
    return rect_;
  }
  TextOffsetRange TextOffset() const {
    return text_offset_;
  }
  TextDirection ResolvedDirection() const {
    return DirectionFromLevel(bidi_level_);
  }
  unsigned BidiLevel() const {
    return bidi_level_;
  }
  size_t FragmentId() const {
    return fragment_id_;
  }
  size_t DeltaToNextForSameLayoutObject() const {
    return delta_to_next_;
  }
  size_t DescendantsCount() const {
    return descendants_count_;
  }
  size_t LineIndex() const {
    return line_index_;
  }
  const ShapeResultView* TextShapeResult() const {
    return shape_.get();
  }
  // The combined text of a 'text-combine-upright: all' box.
  const TextCombine* GetTextCombine() const { return text_combine_.get(); }
  LayoutUnit InlineSize() const {
    return inline_size_;
  }
  LayoutUnit BlockOffset() const {
    return block_offset_;
  }
  LayoutUnit BlockSize() const {
    return block_size_;
  }
  LayoutUnit Baseline() const {
    return baseline_;
  }
  // Content tops of the containing inline boxes, including culled boxes.
  // Stored independently from the text so child vertical-align does not move
  // an ancestor's underline. Objects supply current styles at paint time.
  struct InlinePaintBox {
    const InlineObject* object;
    LayoutUnit block_offset;
  };
  const Vector<InlinePaintBox>& PaintBoxes() const { return paint_boxes_; }
  float CaretInlinePosition(unsigned text_offset, const String&,
                            AdjustMidCluster = AdjustMidCluster::kToEnd) const;
  unsigned TextOffsetForPoint(float inline_position, const String&) const;

private:
  friend class FragmentItems;
  friend class InlineLayoutAlgorithm;
  friend class InlineLayoutStateStack;
  friend class InlineFormattingContext;
  ItemType type_ = kInvalid;
  const InlineObject* object_ = nullptr;
  std::shared_ptr<const ComputedStyle> style_;
  std::shared_ptr<const ShapeResultView> shape_;
  std::shared_ptr<const TextCombine> text_combine_;
  String generated_text_;
  mutable std::shared_ptr<ShapeResult> caret_shape_;
  PhysicalRect rect_;
  TextOffsetRange text_offset_;
  LayoutUnit inline_offset_;
  LayoutUnit block_offset_;
  LayoutUnit inline_size_;
  LayoutUnit block_size_;
  LayoutUnit baseline_;
  Vector<InlinePaintBox> paint_boxes_;
  size_t line_index_ = 0;
  size_t descendants_count_ = 1;
  size_t fragment_id_ = 0;
  size_t delta_to_next_ = 0;
  unsigned bidi_level_ = 0;
  bool is_last_for_node_ = true;
  bool dirty_ = false;
  bool line_break_ = false;
  bool soft_wrap_ = false;
};

} // namespace bkfont
