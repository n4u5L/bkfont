// Ported from: skia/src/ports/SkFontHost_FreeType_common.h

#pragma once

#include <cstdint>
#include <span>

#include <ft2build.h>
#include <freetype/freetype.h>

#include "paint/mask_gamma.h"
#include "paint/matrix.h"
#include "paint/path.h"
#include "platform_glyph.h"
#include "scaler_context.h"

namespace bkit {

class Canvas;

// SkScalerContextFTUtils.
struct ScalerContextFTUtils {
  ColorARGB foreground_color = 0;
  ScalerContext::Flags flags = static_cast<ScalerContext::Flags>(0);

  using LoadGlyphFlags = std::uint32_t;

  void Init(ColorARGB fg_color, ScalerContext::Flags context_flags);

  bool IsSubpixel() const {
    return (flags & ScalerContext::kSubpixelPositioning_Flag) != 0;
  }

  bool IsLinearMetrics() const {
    return (flags & ScalerContext::kLinearMetrics_Flag) != 0;
  }

  bool DrawCOLRv0Glyph(FT_Face face, const PlatformGlyph& glyph, LoadGlyphFlags load_flags,
                       std::span<ColorARGB> palette, Canvas* canvas) const;
  bool DrawCOLRv1Glyph(FT_Face face, const PlatformGlyph& glyph, LoadGlyphFlags load_flags,
                       std::span<ColorARGB> palette, Canvas* canvas) const;
  bool DrawSVGGlyph(FT_Face face, const PlatformGlyph& glyph, LoadGlyphFlags load_flags,
                    std::span<ColorARGB> palette, Canvas* canvas) const;

  // Renders the glyph loaded in the face's slot into image_buffer, which has
  // the glyph's bounds, row bytes and mask format.
  void GenerateGlyphImage(FT_Face face, const PlatformGlyph& glyph, void* image_buffer,
                          const ScalarMatrix& bitmap_transform, const MaskGamma::PreBlend& pre_blend) const;

  // Appends the current outline, converting FreeType's y-up coordinates to
  // y-down. Degenerate segments are omitted as in SkFTGeometrySink.
  static bool GenerateGlyphPath(FT_Face face, ScalarPath* path);

  // Computes a bounding box for a COLRv1 glyph.
  //
  // This method may change the configured size and transforms on FT_Face.
  // Make sure to configure size, matrix and load glyphs as needed after using
  // this function to restore the state of FT_Face.
  static bool ComputeColrV1GlyphBoundingBox(FT_Face face, std::uint16_t glyph_id, ScalarRect* bounds);

private:
  bool GenerateFacePath(FT_Face face, std::uint16_t glyph_id, LoadGlyphFlags load_flags, ScalarPath* path) const;
};

} // namespace bkit
