// Ported from: skia/src/ports/SkFontHost_FreeType_common.h

#pragma once

#include <cstdint>

#include <ft2build.h>
#include <freetype/freetype.h>

#include "rect.h"

namespace bkfont {

// SkScalerContextFTUtils. Only the bounds query used by generateMetrics is
// declared; drawing, paths and init() are not ported.
class ScalerContextFTUtils {
public:
  // Traverses the COLRv1 glyph graph to measure its bounding box. May modify
  // the face's active size.
  static bool ComputeColrV1GlyphBoundingBox(FT_Face face, std::uint16_t glyph_id, ScalarRect* bounds);
};

} // namespace bkfont
