// Ported from: skia/include/core/SkBlendMode.h
// Ported from: skia/src/opts/SkRasterPipeline_opts.h

#pragma once

#include "color4f.h"

namespace bkit {

// SkBlendMode.
enum class BlendMode {
  kClear,      // r = 0
  kSrc,        // r = s
  kDst,        // r = d
  kSrcOver,    // r = s + (1-sa)*d
  kDstOver,    // r = d + (1-da)*s
  kSrcIn,      // r = s * da
  kDstIn,      // r = d * sa
  kSrcOut,     // r = s * (1-da)
  kDstOut,     // r = d * (1-sa)
  kSrcATop,    // r = s*da + d*(1-sa)
  kDstATop,    // r = d*sa + s*(1-da)
  kXor,        // r = s*(1-da) + d*(1-sa)
  kPlus,       // r = min(s + d, 1)
  kModulate,   // r = s*d
  kScreen,     // r = s + d - s*d

  kOverlay,    // multiply or screen, depending on destination
  kDarken,     // rc = s + d - max(s*da, d*sa), ra = kSrcOver
  kLighten,    // rc = s + d - min(s*da, d*sa), ra = kSrcOver
  kColorDodge, // brighten destination to reflect source
  kColorBurn,  // darken destination to reflect source
  kHardLight,  // multiply or screen, depending on source
  kSoftLight,  // lighten or darken, depending on source
  kDifference, // rc = s + d - 2*(min(s*da, d*sa)), ra = kSrcOver
  kExclusion,  // rc = s + d - two(s*d), ra = kSrcOver
  kMultiply,   // r = s*(1-da) + d*(1-sa) + s*d

  kHue,        // hue of source with saturation and luminosity of destination
  kSaturation, // saturation of source with hue and luminosity of destination
  kColor,      // hue and saturation of source with luminosity of destination
  kLuminosity, // luminosity of source with hue and saturation of destination

  kLastCoeffMode = kScreen,
  kLastSeparableMode = kMultiply,
  kLastMode = kLuminosity,
};

// Blends the premultiplied src over the premultiplied dst with the
// highp raster pipeline stage of the mode.
PMColor4f BlendPixel(BlendMode mode, const PMColor4f& src, const PMColor4f& dst);

// True if drawing transparent black with mode can change the destination,
// as SkBlendMode_AffectsTransparentBlack.
bool BlendModeAffectsTransparentBlack(BlendMode mode);

} // namespace bkit
