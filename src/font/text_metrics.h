// Ported from: blink/renderer/platform/fonts/skia/skia_text_metrics.h

#pragma once

#include <span>

#include <hb.h>

#include "glyph.h"
#include "paint/rect.h"

namespace bkit {

class PlatformFont;

// SkFontGetGlyphWidthForHarfBuzz.
void FontGetGlyphWidthForHarfBuzz(const PlatformFont&,
                                  hb_codepoint_t,
                                  hb_position_t* width);
void FontGetGlyphWidthForHarfBuzz(const PlatformFont&,
                                  unsigned count,
                                  const hb_codepoint_t* first_glyph,
                                  unsigned glyph_stride,
                                  hb_position_t* first_advance,
                                  unsigned advance_stride);
// SkFontGetGlyphExtentsForHarfBuzz.
void FontGetGlyphExtentsForHarfBuzz(const PlatformFont&,
                                    hb_codepoint_t,
                                    hb_glyph_extents_t*);

// SkFontGetBoundsForGlyph.
void FontGetBoundsForGlyph(const PlatformFont&, Glyph, ScalarRect* bounds);
// SkFontGetBoundsForGlyphs.
void FontGetBoundsForGlyphs(const PlatformFont&,
                            std::span<const Glyph>,
                            ScalarRect*);
// SkFontGetWidthForGlyph.
float FontGetWidthForGlyph(const PlatformFont&, Glyph);

// SkiaScalarToHarfBuzzPosition.
hb_position_t ScalarToHarfBuzzPosition(float value);

} // namespace bkit
