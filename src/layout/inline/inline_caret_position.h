// Model-position adapter for Blink's inline_caret_position.h.
#pragma once

#include "layout/inline/inline_cursor.h"

namespace bkfont {

enum class InlineCaretPositionType {
  kBeforeBox,
  kAfterBox,
  kAtTextOffset,
  kEmptyLine
};
struct InlineCaretPosition {
  InlineCursor cursor;
  InlineCaretPositionType position_type = InlineCaretPositionType::kAtTextOffset;
  unsigned text_offset = 0;
  explicit operator bool() const {
    return static_cast<bool>(cursor);
  }
};

InlineCaretPosition ComputeInlineCaretPosition(InlineFormattingContext&, InlinePosition);

} // namespace bkfont
