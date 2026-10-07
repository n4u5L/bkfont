// Ported from: skia/src/core/SkMaskGamma.h

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace bkit {

// SkColor, unpremultiplied ARGB.
using ColorARGB = std::uint32_t;

// SkColorGetR, SkColorGetG, SkColorGetB.
inline constexpr unsigned ColorGetR(ColorARGB c) {
  return (c >> 16) & 0xFF;
}
inline constexpr unsigned ColorGetG(ColorARGB c) {
  return (c >> 8) & 0xFF;
}
inline constexpr unsigned ColorGetB(ColorARGB c) {
  return c & 0xFF;
}
// SkColorSetRGB.
inline constexpr ColorARGB ColorSetRGB(unsigned r, unsigned g, unsigned b) {
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// SkComputeLuminance.
inline constexpr unsigned ComputeLuminance(unsigned r, unsigned g, unsigned b) {
  // The following is
  // r * SK_LUM_COEFF_R + g * SK_LUM_COEFF_G + b * SK_LUM_COEFF_B
  // with SK_LUM_COEFF_X in 1.8 fixed point (rounding adjusted to sum to 256).
  return (r * 54 + g * 183 + b * 19) >> 8;
}

// SkColorSpaceLuminance is used to convert luminances to and from linear and
// perceptual color spaces.
//
// Luma is used to specify a linear luminance value [0.0, 1.0].
// Luminance is used to specify a luminance value in an arbitrary color space
// [0.0, 1.0].
class ColorSpaceLuminance {
public:
  ColorSpaceLuminance() = default;
  virtual ~ColorSpaceLuminance() = default;
  ColorSpaceLuminance(const ColorSpaceLuminance&) = delete;
  ColorSpaceLuminance& operator=(const ColorSpaceLuminance&) = delete;

  // Converts a color component luminance in the color space to a linear luma.
  virtual float ToLuma(float gamma, float luminance) const = 0;
  // Converts a linear luma to a color component luminance in the color space.
  virtual float FromLuma(float gamma, float luma) const = 0;

  // Retrieves the ColorSpaceLuminance for the given gamma.
  static const ColorSpaceLuminance& Fetch(float gamma);
};

// sk_t_scale255. Scales base <= 2^N-1 to 2^8-1.
// N is [1, 8], the number of bits used by base.
template <unsigned N>
inline unsigned TScale255(unsigned base) {
  if constexpr (N == 1) {
    return base * 0xFF;
  } else if constexpr (N == 2) {
    return base * 0x55;
  } else if constexpr (N == 4) {
    return base * 0x11;
  } else if constexpr (N == 8) {
    return base;
  } else {
    base <<= (8 - N);
    unsigned lum = base;
    for (unsigned int i = N; i < 8; i += N) {
      lum |= base >> i;
    }
    return lum;
  }
}

template <int R_LUM_BITS, int G_LUM_BITS, int B_LUM_BITS>
class TMaskPreBlend;

void TMaskGammaBuildCorrectingLut(std::uint8_t* table, unsigned src_i, float contrast,
                                  const ColorSpaceLuminance& dst_convert, float dst_gamma);

// SkTMaskGamma. A regular mask contains linear alpha values. A gamma
// correcting mask contains non-linear alpha values in an attempt to create
// gamma correct blits in the presence of a gamma incorrect (linear) blend in
// the blitter.
//
// TMaskGamma creates and maintains tables which convert linear alpha values to
// gamma correcting alpha values.
// R, G and B are the number of luminance bits to use [1, 8] from each channel.
// Instances are owned by std::shared_ptr, which stands in for SkRefCnt.
template <int R_LUM_BITS, int G_LUM_BITS, int B_LUM_BITS>
class TMaskGamma : public std::enable_shared_from_this<TMaskGamma<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>> {
public:
  // Creates a linear TMaskGamma.
  TMaskGamma() = default;

  // Creates tables to convert linear alpha values to gamma correcting alpha
  // values.
  //
  // contrast is a value in the range [0.0, 1.0] which indicates the amount of
  // artificial contrast to add. device_gamma is the gamma of the target
  // device.
  TMaskGamma(float contrast, float device_gamma)
      : gamma_tables_(std::make_unique<std::uint8_t[]>(kTableNumElements)) {
    const ColorSpaceLuminance& device_convert = ColorSpaceLuminance::Fetch(device_gamma);
    for (unsigned i = 0; i < kNumTables; ++i) {
      unsigned lum = TScale255<kMaxLumBits>(i);
      TMaskGammaBuildCorrectingLut(&gamma_tables_[i * kTableWidth], lum, contrast, device_convert, device_gamma);
    }
  }

  TMaskGamma(const TMaskGamma&) = delete;
  TMaskGamma& operator=(const TMaskGamma&) = delete;

  // Given a color, returns the closest canonical color.
  static ColorARGB CanonicalColor(ColorARGB color) {
    return ColorSetRGB(TScale255<R_LUM_BITS>(ColorGetR(color) >> (8 - R_LUM_BITS)),
                       TScale255<G_LUM_BITS>(ColorGetG(color) >> (8 - G_LUM_BITS)),
                       TScale255<B_LUM_BITS>(ColorGetB(color) >> (8 - B_LUM_BITS)));
  }

  // The type of the mask pre-blend which will be returned from
  // PreBlend(ColorARGB).
  using PreBlend = TMaskPreBlend<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>;

  // Provides access to the tables appropriate for converting linear alpha
  // values into gamma correcting alpha values when drawing the given color
  // through the mask. The destination color will be approximated.
  PreBlend MakePreBlend(ColorARGB color) const;

  // Get dimensions for the full table set, so it can be allocated as a block.
  // Linear tables should report the full table size.
  void GetGammaTableDimensions(int* table_width, int* num_tables) const {
    *table_width = static_cast<int>(kTableWidth);
    *num_tables = static_cast<int>(kNumTables);
  }

  // Returns the size for the full table set in bytes, so it can be allocated
  // as a block. Linear tables should report the full table size.
  constexpr std::size_t GetGammaTableSizeInBytes() const {
    return kTableNumElements * sizeof(std::uint8_t);
  }

  // Provides direct access to the full table set, so it can be uploaded into
  // a texture or analyzed in other ways. Returns nullptr if gamma_tables_
  // hasn't been initialized.
  const std::uint8_t* GetGammaTables() const {
    return gamma_tables_.get();
  }

private:
  static constexpr int kMaxLumBits = std::max({B_LUM_BITS, R_LUM_BITS, G_LUM_BITS});
  static constexpr std::size_t kNumTables = 1 << kMaxLumBits;
  static constexpr std::size_t kTableWidth = 256;
  static constexpr std::size_t kTableNumElements = kNumTables * kTableWidth;

  bool IsLinear() const {
    return gamma_tables_ == nullptr;
  }

  // gamma_tables_ is a flattened 2-D array. Accessing rows requires accounting
  // for the width dimension (via kTableWidth).
  std::unique_ptr<std::uint8_t[]> gamma_tables_;
};

// SkTMaskPreBlend. A tear-off of TMaskGamma. It provides the tables to
// convert a linear alpha value for a given channel to a gamma correcting
// alpha value for that channel. This class is immutable.
//
// If r, g, or b is nullptr, all of them will be. This indicates that no mask
// pre blend should be applied. IsApplicable() is provided as a convenience
// function to test for the absence of this case.
template <int R_LUM_BITS, int G_LUM_BITS, int B_LUM_BITS>
class TMaskPreBlend {
private:
  TMaskPreBlend(std::shared_ptr<const TMaskGamma<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>> parent,
                const std::uint8_t* r_table, const std::uint8_t* g_table, const std::uint8_t* b_table)
      : parent_(std::move(parent)), r(r_table), g(g_table), b(b_table) {
  }

  std::shared_ptr<const TMaskGamma<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>> parent_;
  friend class TMaskGamma<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>;

public:
  // Creates a non applicable TMaskPreBlend.
  TMaskPreBlend()
      : r(nullptr), g(nullptr), b(nullptr) {
  }

  // True if this PreBlend should be applied. When false, r, g, and b are
  // nullptr.
  bool IsApplicable() const {
    return g != nullptr;
  }

  const std::uint8_t* r;
  const std::uint8_t* g;
  const std::uint8_t* b;
};

template <int R_LUM_BITS, int G_LUM_BITS, int B_LUM_BITS>
TMaskPreBlend<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>
TMaskGamma<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>::MakePreBlend(ColorARGB color) const {
  if (IsLinear()) {
    return TMaskPreBlend<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>();
  }
  constexpr std::size_t kLumShift = 8 - kMaxLumBits;
  const std::size_t r_index = (ColorGetR(color) >> kLumShift) * kTableWidth;
  const std::size_t g_index = (ColorGetG(color) >> kLumShift) * kTableWidth;
  const std::size_t b_index = (ColorGetB(color) >> kLumShift) * kTableWidth;
  return TMaskPreBlend<R_LUM_BITS, G_LUM_BITS, B_LUM_BITS>(this->shared_from_this(),
                                                           &gamma_tables_[r_index],
                                                           &gamma_tables_[g_index],
                                                           &gamma_tables_[b_index]);
}

// sk_apply_lut_if. If APPLY_LUT is false, returns component unchanged. If
// APPLY_LUT is true, returns lut[component].
template <bool APPLY_LUT>
inline unsigned ApplyLutIf(unsigned component, const std::uint8_t* lut) {
  if constexpr (APPLY_LUT) {
    return lut[component];
  } else {
    return component;
  }
}

} // namespace bkit
