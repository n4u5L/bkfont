// Ported from: skia/src/ports/SkFontHost_FreeType_common.cpp

#include "font_host_freetype_common.h"

namespace bkfont {

// TODO: port colrv1_start_glyph_bounds and its paint graph traversal. Until
// then a COLRv1 glyph without a ClipBox gets empty bounds. The traversal only
// produces bounds, so the advance is unaffected unless the graph is invalid,
// in which case upstream returns false and the advance is zero.
bool ScalerContextFTUtils::ComputeColrV1GlyphBoundingBox(FT_Face, std::uint16_t, ScalarRect* bounds) {
  *bounds = ScalarRect();
  return true;
}

} // namespace bkfont
