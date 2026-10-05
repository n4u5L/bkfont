// Ported from: skia/src/opts/SkRasterPipeline_opts.h
// Ported from: skia/src/core/SkBlendMode.cpp

#include "blend_mode.h"

#include <algorithm>
#include <cmath>

namespace bkfont {

namespace {

float Inv(float x) {
  return 1.0f - x;
}
float Two(float x) {
  return x + x;
}
float Mad(float f, float m, float a) {
  return f * m + a;
}
float Nmad(float f, float m, float a) {
  return -f * m + a;
}

// Most blend modes apply the same logic to each channel.
template <typename Channel>
PMColor4f BlendAllChannels(const PMColor4f& s, const PMColor4f& d, Channel channel) {
  return {channel(s.r, d.r, s.a, d.a), channel(s.g, d.g, s.a, d.a), channel(s.b, d.b, s.a, d.a), channel(s.a, d.a, s.a, d.a)};
}

// Most other blend modes apply the same logic to colors, and srcover to
// alpha.
template <typename Channel>
PMColor4f BlendColorChannels(const PMColor4f& s, const PMColor4f& d, Channel channel) {
  return {channel(s.r, d.r, s.a, d.a), channel(s.g, d.g, s.a, d.a), channel(s.b, d.b, s.a, d.a), Mad(d.a, Inv(s.a), s.a)};
}

float ColorBurnChannel(float s, float d, float sa, float da) {
  if (d == da) {
    return d + s * Inv(da);
  }
  if (s == 0) {
    return /* s + */ d * Inv(sa);
  }
  return sa * (da - std::min(da, (da - d) * sa * (1 / s))) + s * Inv(da) + d * Inv(sa);
}

float ColorDodgeChannel(float s, float d, float sa, float da) {
  if (d == 0) {
    return /* d + */ s * Inv(da);
  }
  if (s == sa) {
    return s + d * Inv(sa);
  }
  return sa * std::min(da, (d * sa) * (1 / (sa - s))) + s * Inv(da) + d * Inv(sa);
}

float HardLightChannel(float s, float d, float sa, float da) {
  return s * Inv(da) + d * Inv(sa) + (Two(s) <= sa ? Two(s * d) : sa * da - Two((da - d) * (sa - s)));
}

float OverlayChannel(float s, float d, float sa, float da) {
  return s * Inv(da) + d * Inv(sa) + (Two(d) <= da ? Two(s * d) : sa * da - Two((da - d) * (sa - s)));
}

float SoftLightChannel(float s, float d, float sa, float da) {
  float m = da > 0 ? d / da : 0.0f;
  float s2 = Two(s);
  float m4 = Two(Two(m));

  // The logic forks three ways:
  //    1. dark src?
  //    2. light src, dark dst?
  //    3. light src, light dst?
  float dark_src = d * (sa + (s2 - sa) * (1.0f - m));    // Used in case 1.
  float dark_dst = (m4 * m4 + m4) * (m - 1.0f) + 7.0f * m; // Used in case 2.
  float lite_dst = std::sqrt(m) - m;
  float lite_src = d * sa + da * (s2 - sa) * (Two(Two(d)) <= da ? dark_dst : lite_dst); // 2 or 3?
  return s * Inv(da) + d * Inv(sa) + (s2 <= sa ? dark_src : lite_src);                 // 1 or (2 or 3)?
}

// We're basing our implemenation of non-separable blend modes on
//   https://www.w3.org/TR/compositing-1/#blendingnonseparable.
// and
//   https://www.khronos.org/registry/OpenGL/specs/es/3.2/es_spec_3.2.pdf
// They're equivalent, but ES' math has been better simplified.
//
// Anything extra we add beyond that is to make the math work with premul
// inputs.

float Sat(float r, float g, float b) {
  return std::max(r, std::max(g, b)) - std::min(r, std::min(g, b));
}
float Lum(float r, float g, float b) {
  return Mad(r, 0.30f, Mad(g, 0.59f, b * 0.11f));
}

void SetSat(float* r, float* g, float* b, float s) {
  float mn = std::min(*r, std::min(*g, *b));
  float mx = std::max(*r, std::max(*g, *b));
  float sat = mx - mn;

  // Map min channel to 0, max channel to s, and scale the middle
  // proportionally.
  s = sat == 0.0f ? 0.0f : s * (1 / sat);
  *r = (*r - mn) * s;
  *g = (*g - mn) * s;
  *b = (*b - mn) * s;
}

void SetLum(float* r, float* g, float* b, float l) {
  float diff = l - Lum(*r, *g, *b);
  *r += diff;
  *g += diff;
  *b += diff;
}

float ClipChannel(float c, float l, bool clip_low, bool clip_high, float mn_scale, float mx_scale) {
  c = clip_low ? Mad(mn_scale, c - l, l) : c;
  c = clip_high ? Mad(mx_scale, c - l, l) : c;
  c = std::max(c, 0.0f); // Sometimes without this we may dip just a little negative.
  return c;
}

void ClipColor(float* r, float* g, float* b, float a) {
  float mn = std::min(*r, std::min(*g, *b));
  float mx = std::max(*r, std::max(*g, *b));
  float l = Lum(*r, *g, *b);
  float mn_scale = (l) * (1 / (l - mn));
  float mx_scale = (a - l) * (1 / (mx - l));
  bool clip_low = mn < 0 && l != mn;
  bool clip_high = mx > a && l != mx;

  *r = ClipChannel(*r, l, clip_low, clip_high, mn_scale, mx_scale);
  *g = ClipChannel(*g, l, clip_low, clip_high, mn_scale, mx_scale);
  *b = ClipChannel(*b, l, clip_low, clip_high, mn_scale, mx_scale);
}

PMColor4f NonSeparableResult(const PMColor4f& s, const PMColor4f& d, float rr, float gg, float bb) {
  return {Mad(s.r, Inv(d.a), Mad(d.r, Inv(s.a), rr)),
          Mad(s.g, Inv(d.a), Mad(d.g, Inv(s.a), gg)),
          Mad(s.b, Inv(d.a), Mad(d.b, Inv(s.a), bb)),
          s.a + Nmad(s.a, d.a, d.a)};
}

PMColor4f HueBlend(const PMColor4f& s, const PMColor4f& d) {
  float r = s.r * s.a;
  float g = s.g * s.a;
  float b = s.b * s.a;

  SetSat(&r, &g, &b, Sat(d.r, d.g, d.b) * s.a);
  SetLum(&r, &g, &b, Lum(d.r, d.g, d.b) * s.a);
  ClipColor(&r, &g, &b, s.a * d.a);
  return NonSeparableResult(s, d, r, g, b);
}

PMColor4f SaturationBlend(const PMColor4f& s, const PMColor4f& d) {
  float r = d.r * s.a;
  float g = d.g * s.a;
  float b = d.b * s.a;

  SetSat(&r, &g, &b, Sat(s.r, s.g, s.b) * d.a);
  SetLum(&r, &g, &b, Lum(d.r, d.g, d.b) * s.a); // (This is not redundant.)
  ClipColor(&r, &g, &b, s.a * d.a);
  return NonSeparableResult(s, d, r, g, b);
}

PMColor4f ColorBlend(const PMColor4f& s, const PMColor4f& d) {
  float r = s.r * d.a;
  float g = s.g * d.a;
  float b = s.b * d.a;

  SetLum(&r, &g, &b, Lum(d.r, d.g, d.b) * s.a);
  ClipColor(&r, &g, &b, s.a * d.a);
  return NonSeparableResult(s, d, r, g, b);
}

PMColor4f LuminosityBlend(const PMColor4f& s, const PMColor4f& d) {
  float r = d.r * s.a;
  float g = d.g * s.a;
  float b = d.b * s.a;

  SetLum(&r, &g, &b, Lum(s.r, s.g, s.b) * d.a);
  ClipColor(&r, &g, &b, s.a * d.a);
  return NonSeparableResult(s, d, r, g, b);
}

} // namespace

PMColor4f BlendPixel(BlendMode mode, const PMColor4f& s, const PMColor4f& d) {
  switch (mode) {
  case BlendMode::kClear:
    return {0, 0, 0, 0};
  case BlendMode::kSrc:
    return s;
  case BlendMode::kDst:
    return d;
  case BlendMode::kSrcOver:
    return BlendAllChannels(s, d, [](float sc, float dc, float sa, float) { return Mad(dc, Inv(sa), sc); });
  case BlendMode::kDstOver:
    return BlendAllChannels(s, d, [](float sc, float dc, float, float da) { return Mad(sc, Inv(da), dc); });
  case BlendMode::kSrcIn:
    return BlendAllChannels(s, d, [](float sc, float, float, float da) { return sc * da; });
  case BlendMode::kDstIn:
    return BlendAllChannels(s, d, [](float, float dc, float sa, float) { return dc * sa; });
  case BlendMode::kSrcOut:
    return BlendAllChannels(s, d, [](float sc, float, float, float da) { return sc * Inv(da); });
  case BlendMode::kDstOut:
    return BlendAllChannels(s, d, [](float, float dc, float sa, float) { return dc * Inv(sa); });
  case BlendMode::kSrcATop:
    return BlendAllChannels(s, d, [](float sc, float dc, float sa, float da) { return Mad(sc, da, dc * Inv(sa)); });
  case BlendMode::kDstATop:
    return BlendAllChannels(s, d, [](float sc, float dc, float sa, float da) { return Mad(dc, sa, sc * Inv(da)); });
  case BlendMode::kXor:
    return BlendAllChannels(s, d, [](float sc, float dc, float sa, float da) { return Mad(sc, Inv(da), dc * Inv(sa)); });
  case BlendMode::kPlus:
    // We can clamp to either 1 or sa.
    return BlendAllChannels(s, d, [](float sc, float dc, float, float) { return std::min(sc + dc, 1.0f); });
  case BlendMode::kModulate:
    return BlendAllChannels(s, d, [](float sc, float dc, float, float) { return sc * dc; });
  case BlendMode::kScreen:
    return BlendAllChannels(s, d, [](float sc, float dc, float, float) { return Nmad(sc, dc, sc + dc); });
  case BlendMode::kOverlay:
    return BlendColorChannels(s, d, OverlayChannel);
  case BlendMode::kDarken:
    return BlendColorChannels(s, d, [](float sc, float dc, float sa, float da) { return sc + dc - std::max(sc * da, dc * sa); });
  case BlendMode::kLighten:
    return BlendColorChannels(s, d, [](float sc, float dc, float sa, float da) { return sc + dc - std::min(sc * da, dc * sa); });
  case BlendMode::kColorDodge:
    return BlendColorChannels(s, d, ColorDodgeChannel);
  case BlendMode::kColorBurn:
    return BlendColorChannels(s, d, ColorBurnChannel);
  case BlendMode::kHardLight:
    return BlendColorChannels(s, d, HardLightChannel);
  case BlendMode::kSoftLight:
    return BlendColorChannels(s, d, SoftLightChannel);
  case BlendMode::kDifference:
    return BlendColorChannels(s, d, [](float sc, float dc, float sa, float da) { return sc + dc - Two(std::min(sc * da, dc * sa)); });
  case BlendMode::kExclusion:
    return BlendColorChannels(s, d, [](float sc, float dc, float, float) { return sc + dc - Two(sc * dc); });
  case BlendMode::kMultiply:
    return BlendAllChannels(s, d, [](float sc, float dc, float sa, float da) { return Mad(sc, dc, Mad(sc, Inv(da), dc * Inv(sa))); });
  case BlendMode::kHue:
    return HueBlend(s, d);
  case BlendMode::kSaturation:
    return SaturationBlend(s, d);
  case BlendMode::kColor:
    return ColorBlend(s, d);
  case BlendMode::kLuminosity:
    return LuminosityBlend(s, d);
  }
  return s;
}

bool BlendModeAffectsTransparentBlack(BlendMode mode) {
  // The modes whose destination coefficient is not one for a transparent
  // source.
  switch (mode) {
  case BlendMode::kClear:
  case BlendMode::kSrc:
  case BlendMode::kSrcIn:
  case BlendMode::kDstIn:
  case BlendMode::kSrcOut:
  case BlendMode::kDstATop:
  case BlendMode::kModulate:
    return true;
  default:
    return false;
  }
}

} // namespace bkfont
