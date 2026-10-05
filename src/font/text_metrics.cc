// Ported from: blink/renderer/platform/fonts/skia/skia_text_metrics.cc

#include "text_metrics.h"

#include <cmath>
#include <cstdint>

#include "base/math_extras.h"
#include "base/vector.h"
#include "platform/platform_font.h"
#include "paint/scalar.h"
#include "shaping/harfbuzz_face.h"

namespace bkfont {

namespace {

template <class T>
T* advance_by_byte_size(T* p, unsigned byte_size) {
  return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(p) + byte_size);
}

template <class T>
const T* advance_by_byte_size(const T* p, unsigned byte_size) {
  return reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(p) +
                                    byte_size);
}

// SkRect::roundOut() into an SkIRect, then SkRect::set(const SkIRect&).
ScalarRect RoundOutToIRect(const ScalarRect& r) {
  return ScalarRect::MakeLTRB(
      static_cast<float>(FloatSaturateToInt(std::floor(r.left))),
      static_cast<float>(FloatSaturateToInt(std::floor(r.top))),
      static_cast<float>(FloatSaturateToInt(std::ceil(r.right))),
      static_cast<float>(FloatSaturateToInt(std::ceil(r.bottom))));
}

} // namespace

void FontGetGlyphWidthForHarfBuzz(const PlatformFont& font,
                                  hb_codepoint_t codepoint,
                                  hb_position_t* width) {
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

void FontGetGlyphWidthForHarfBuzz(const PlatformFont& font,
                                  unsigned count,
                                  const hb_codepoint_t* glyphs,
                                  const unsigned glyph_stride,
                                  hb_position_t* advances,
                                  unsigned advance_stride) {
  // Batch the call to getWidths because its function entry cost is not
  // cheap. getWidths accepts multiple glyphd ID, but not from a sparse
  // array that copy them to a regular array.
  Vector<Glyph, 256> glyph_array(count);
  for (unsigned i = 0; i < count;
       i++, glyphs = advance_by_byte_size(glyphs, glyph_stride)) {
    glyph_array[i] = static_cast<Glyph>(*glyphs);
  }
  Vector<float, 256> sk_width_array(count);
  font.GetWidths({glyph_array.data(), glyph_array.size()}, {sk_width_array.data(), sk_width_array.size()});

  if (!font.IsSubpixel()) {
    for (unsigned i = 0; i < count; i++)
      sk_width_array[i] = static_cast<float>(FloatRoundToInt(sk_width_array[i]));
  }

  // Copy the results back to the sparse array.
  for (unsigned i = 0; i < count;
       i++, advances = advance_by_byte_size(advances, advance_stride)) {
    *advances = ScalarToHarfBuzzPosition(sk_width_array[i]);
  }
}

// HarfBuzz callback to retrieve glyph extents, mainly used by HarfBuzz for
// fallback mark positioning, i.e. the situation when the font does not have
// mark anchors or other mark positioning rules, but instead HarfBuzz is
// supposed to heuristically place combining marks around base glyphs. HarfBuzz
// does this by measuring "ink boxes" of glyphs, and placing them according to
// Unicode mark classes. Above, below, centered or left or right, etc.
void FontGetGlyphExtentsForHarfBuzz(const PlatformFont& font,
                                    hb_codepoint_t codepoint,
                                    hb_glyph_extents_t* extents) {
  // We don't want to compute glyph extents for kUnmatchedVSGlyphId
  // cases yet. Since we will do that during the second shaping pass,
  // when VariationSelectorMode is set to kIgnoreVariationSelector.
  if (codepoint == kUnmatchedVSGlyphId) {
    return;
  }

  ScalarRect sk_bounds;
  std::uint16_t glyph = static_cast<std::uint16_t>(codepoint);

  // The IS_APPLE path-bounds workaround targets CoreText metrics; this port
  // rasterizes with FreeType everywhere.
  sk_bounds = font.GetBounds(glyph);
  if (!font.IsSubpixel()) {
    // Use roundOut() rather than round() to avoid rendering glyphs
    // outside the visual overflow rect. crbug.com/452914.
    sk_bounds = RoundOutToIRect(sk_bounds);
  }

  // Invert y-axis because Skia is y-grows-down but we set up HarfBuzz to be
  // y-grows-up.
  extents->x_bearing = ScalarToHarfBuzzPosition(sk_bounds.left);
  extents->y_bearing = ScalarToHarfBuzzPosition(-sk_bounds.top);
  extents->width = ScalarToHarfBuzzPosition(sk_bounds.Width());
  extents->height = ScalarToHarfBuzzPosition(-sk_bounds.Height());
}

void FontGetBoundsForGlyph(const PlatformFont& font, Glyph glyph, ScalarRect* bounds) {
  *bounds = font.GetBounds(glyph);

  if (!font.IsSubpixel()) {
    *bounds = RoundOutToIRect(*bounds);
  }
}

void FontGetBoundsForGlyphs(const PlatformFont& font,
                            std::span<const Glyph> glyphs,
                            ScalarRect* bounds) {
  static_assert(sizeof(Glyph) == 2, "Skia expects 2 bytes glyph id.");
  font.GetBounds(glyphs, {bounds, glyphs.size()});

  if (!font.IsSubpixel()) {
    for (std::size_t i = 0; i < glyphs.size(); i++) {
      bounds[i] = RoundOutToIRect(bounds[i]);
    }
  }
}

float FontGetWidthForGlyph(const PlatformFont& font, Glyph glyph) {
  float sk_width = font.GetWidth(glyph);

  if (!font.IsSubpixel())
    sk_width = static_cast<float>(FloatRoundToInt(sk_width));

  return sk_width;
}

hb_position_t ScalarToHarfBuzzPosition(float value) {
  // We treat HarfBuzz hb_position_t as 16.16 fixed-point.
  static const int kHbPosition1 = 1 << 16;
  return ClampTo<int>(value * kHbPosition1);
}

} // namespace bkfont
