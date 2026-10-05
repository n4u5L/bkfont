// Ported from: blink/renderer/platform/fonts/text_fragment_paint_info.cc

#include "text_fragment_paint_info.h"

namespace bkfont {

TextFragmentPaintInfo TextFragmentPaintInfo::Slice(unsigned slice_from,
                                                   unsigned slice_to) const {
  return {text, slice_from, slice_to, shape_result};
}

TextFragmentPaintInfo TextFragmentPaintInfo::WithStartOffset(
    unsigned start_from) const {
  return Slice(start_from, to);
}

TextFragmentPaintInfo TextFragmentPaintInfo::WithEndOffset(
    unsigned end_to) const {
  return Slice(from, end_to);
}

} // namespace bkfont
