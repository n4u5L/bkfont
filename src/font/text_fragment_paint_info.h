// Ported from: blink/renderer/platform/fonts/text_fragment_paint_info.h

#pragma once

#include "base/text/string_view.h"
#include "platform_export.h"

namespace bkit {

class ShapeResultView;

// Bridge struct for painting text. Encapsulates info needed by the paint
// code.
struct PLATFORM_EXPORT TextFragmentPaintInfo {
public:
  TextFragmentPaintInfo Slice(unsigned slice_from, unsigned slice_to) const;
  TextFragmentPaintInfo WithStartOffset(unsigned start_from) const;
  TextFragmentPaintInfo WithEndOffset(unsigned end_to) const;
  unsigned Length() const {
    return to - from;
  }

  // The string to paint. May include surrounding context.
  const StringView text;

  // The range of the |text| to paint.
  unsigned from;
  unsigned to;

  // The |shape_result| may not contain all characters of the |text|, but is
  // guaranteed to contain |from| to |to|.
  const ShapeResultView* shape_result;
};

} // namespace bkit
