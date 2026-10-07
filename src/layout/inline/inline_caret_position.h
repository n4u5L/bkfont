// Model-position adapter for Blink's inline_caret_position.h.
#pragma once

#include "layout/inline/inline_cursor.h"

namespace bkit {

enum class InlineCaretPositionType {
  kBeforeBox,
  kAfterBox,
  kAtTextOffset
};
struct InlineCaretPosition {
  InlineCursor cursor;
  InlineCaretPositionType position_type = InlineCaretPositionType::kAtTextOffset;
  unsigned text_offset = 0;
  explicit operator bool() const {
    return static_cast<bool>(cursor);
  }
  // Before/after the box for box positions. A text offset maps through the
  // OffsetMapping: the last position for downstream affinity (not at the
  // item's end), the first position for upstream affinity.
  InlinePosition ToPositionInDOMTreeWithAffinity(const OffsetMapping&) const;
};

InlineCaretPosition ComputeInlineCaretPosition(InlineFormattingContext&, InlinePosition);

} // namespace bkit
