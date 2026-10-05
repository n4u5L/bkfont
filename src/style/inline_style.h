// Compatibility input for the pre-declaration IFC API. All lengths and font
// sizes are supplied before device/page zoom. New code uses StyleDeclaration.
#pragma once

#include "font/font.h"
#include "geometry/length.h"
#include "layout/layout_unit.h"
#include "paint/platform_paint.h"
#include "text/tab_size.h"

namespace bkfont {

struct InlineStyle {
  explicit InlineStyle(Font font) : font(std::move(font)) {}
  Font font;
  Length line_height = Length::Auto();
  PlatformPaint paint;
  TabSize tab_size{8};
  // Compatibility with the old IFC API, not a CSS vertical-align property.
  LayoutUnit baseline_shift;
  InlineStyle Zoom(float factor) const;
};

} // namespace bkfont
