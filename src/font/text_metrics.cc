// Ported from: blink/renderer/platform/fonts/skia/skia_text_metrics.cc

#include "text_metrics.h"

#include <cstdint>

#include "base/math_extras.h"
#include "platform/platform_font.h"
#include "platform/scalar.h"
#include "shaping/harfbuzz_face.h"

namespace bkfont {

void FontGetGlyphWidthForHarfBuzz(const PlatformFont& font, hb_codepoint_t codepoint, hb_position_t* width) {
  // We don't want to compute glyph extents for kUnmatchedVSGlyphId
  // cases yet. Since we will do that during the second shaping pass,
  // when VariationSelectorMode is set to kIgnoreVariationSelector.
  if (codepoint == kUnmatchedVSGlyphId) {
    return;
  }

  std::uint16_t glyph = static_cast<std::uint16_t>(codepoint);
  float sk_width = font.GetWidth(glyph);

  if (!font.IsSubpixel())
    sk_width = static_cast<float>(FloatRoundToInt(sk_width));
  *width = ScalarToHarfBuzzPosition(sk_width);
}

hb_position_t ScalarToHarfBuzzPosition(float value) {
  // We treat HarfBuzz hb_position_t as 16.16 fixed-point.
  static const int kHbPosition1 = 1 << 16;
  return ClampTo<int>(value * kHbPosition1);
}

} // namespace bkfont
