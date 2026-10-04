// Ported from: blink/renderer/platform/fonts/skia/skia_text_metrics.h

#pragma once

#include <hb.h>

namespace bkfont {

class PlatformFont;

// SkFontGetGlyphWidthForHarfBuzz.
void FontGetGlyphWidthForHarfBuzz(const PlatformFont&, hb_codepoint_t, hb_position_t* width);

// SkiaScalarToHarfBuzzPosition.
hb_position_t ScalarToHarfBuzzPosition(float value);

} // namespace bkfont
