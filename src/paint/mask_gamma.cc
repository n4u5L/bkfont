// Ported from: skia/src/core/SkMaskGamma.cpp

#include "mask_gamma.h"

#include <cmath>

#include "scalar.h"

namespace bkit {

namespace {

class LinearColorSpaceLuminance final : public ColorSpaceLuminance {
public:
  float ToLuma(float, float luminance) const override {
    return luminance;
  }
  float FromLuma(float, float luma) const override {
    return luma;
  }
};

class GammaColorSpaceLuminance final : public ColorSpaceLuminance {
public:
  float ToLuma(float gamma, float luminance) const override {
    return std::pow(luminance, gamma);
  }
  float FromLuma(float gamma, float luma) const override {
    return std::pow(luma, 1 / gamma);
  }
};

class SRGBColorSpaceLuminance final : public ColorSpaceLuminance {
public:
  float ToLuma(float, float luminance) const override {
    // The magic numbers are derived from the sRGB specification.
    // See http://www.color.org/chardata/rgb/srgb.xalter .
    if (luminance <= 0.04045f) {
      return luminance / 12.92f;
    }
    return std::pow((luminance + 0.055f) / 1.055f, 2.4f);
  }
  float FromLuma(float, float luma) const override {
    // The magic numbers are derived from the sRGB specification.
    // See http://www.color.org/chardata/rgb/srgb.xalter .
    if (luma <= 0.0031308f) {
      return luma * 12.92f;
    }
    return 1.055f * std::pow(luma, 1 / 2.4f) - 0.055f;
  }
};

float ApplyContrast(float srca, float contrast) {
  return srca + ((1.0f - srca) * contrast * srca);
}

} // namespace

const ColorSpaceLuminance& ColorSpaceLuminance::Fetch(float gamma) {
  static const LinearColorSpaceLuminance linear_color_space_luminance;
  static const GammaColorSpaceLuminance gamma_color_space_luminance;
  static const SRGBColorSpaceLuminance srgb_color_space_luminance;

  if (0 == gamma) {
    return srgb_color_space_luminance;
  } else if (1 == gamma) {
    return linear_color_space_luminance;
  } else {
    return gamma_color_space_luminance;
  }
}

void TMaskGammaBuildCorrectingLut(std::uint8_t* table, unsigned src_i, float contrast,
                                  const ColorSpaceLuminance& dst_convert, float dst_gamma) {
  const ColorSpaceLuminance& src_convert = dst_convert;
  const float src_gamma = dst_gamma;
  const float src = static_cast<float>(src_i) / 255.0f;
  const float lin_src = src_convert.ToLuma(src_gamma, src);
  // Guess at the dst. The perceptual inverse provides smaller visual
  // discontinuities when slight changes to desaturated colors cause a channel
  // to map to a different correcting lut with neighboring src_i.
  // See https://code.google.com/p/chromium/issues/detail?id=141425#c59 .
  const float dst = 1.0f - src;
  const float lin_dst = dst_convert.ToLuma(dst_gamma, dst);

  // Contrast value tapers off to 0 as the src luminance becomes white
  const float adjusted_contrast = contrast * lin_dst;

  // Remove discontinuity and instability when src is close to dst.
  // The value 1/256 is arbitrary and appears to contain the instability.
  if (std::fabs(src - dst) < (1.0f / 256.0f)) {
    float ii = 0.0f;
    for (int i = 0; i < 256; ++i, ii += 1.0f) {
      float raw_srca = ii / 255.0f;
      float srca = ApplyContrast(raw_srca, adjusted_contrast);
      table[i] = static_cast<std::uint8_t>(FloatRoundToInt(255.0f * srca));
    }
  } else {
    // Avoid slow int to float conversion.
    float ii = 0.0f;
    for (int i = 0; i < 256; ++i, ii += 1.0f) {
      // 'raw_srca += 1.0f / 255.0f' and even
      // 'raw_srca = i * (1.0f / 255.0f)' can add up to more than 1.0f.
      // When this happens the table[255] == 0x0 instead of 0xff.
      // See http://code.google.com/p/chromium/issues/detail?id=146466
      float raw_srca = ii / 255.0f;
      float srca = ApplyContrast(raw_srca, adjusted_contrast);
      float dsta = 1.0f - srca;

      // Calculate the output we want.
      float lin_out = (lin_src * srca + dsta * lin_dst);
      float out = dst_convert.FromLuma(dst_gamma, lin_out);

      // Undo what the blit blend will do.
      float result = (out - dst) / (src - dst);

      table[i] = static_cast<std::uint8_t>(FloatRoundToInt(255.0f * result));
    }
  }
}

} // namespace bkit
