// Ported from: skia/include/core/SkSurfaceProps.h
// Ported from: skia/src/image/SkSurface.cpp

#pragma once

#include <cstdint>

namespace bkit {

// SK_GAMMA_EXPONENT and SK_GAMMA_CONTRAST as Chromium's skia/BUILD.gn sets
// them for Linux, which every platform follows here. SK_GAMMA_APPLY_TO_A8 is
// not defined there.
inline constexpr float kGammaExponent = 1.2f;
inline constexpr float kGammaContrast = 0.2f;

// SkPixelGeometry. Description of how the LCD strips are arranged for each
// pixel. If this is unknown, or the pixels are meant to be "portable" and/or
// transformed before showing (e.g. rotated, scaled) then use kUnknown.
enum class PixelGeometry {
  kUnknown,
  kRGB_H,
  kBGR_H,
  kRGB_V,
  kBGR_V,
};

// SkPixelGeometryIsH, SkPixelGeometryIsV.
inline bool PixelGeometryIsH(PixelGeometry geo) {
  return geo == PixelGeometry::kRGB_H || geo == PixelGeometry::kBGR_H;
}
inline bool PixelGeometryIsV(PixelGeometry geo) {
  return geo == PixelGeometry::kRGB_V || geo == PixelGeometry::kBGR_V;
}

// SkSurfaceProps. Describes properties and constraints of a given surface.
class SurfaceProps {
public:
  enum Flags : std::uint32_t {
    kDefault_Flag = 0,
    kUseDeviceIndependentFonts_Flag = 1 << 0,
    // Use internal MSAA to render to non-MSAA GPU surfaces.
    kDynamicMSAA_Flag = 1 << 1,
    // If set, all rendering will have dithering enabled.
    // Currently this only impacts GPU backends.
    kAlwaysDither_Flag = 1 << 2,
  };

  // No flags, unknown pixel geometry, platform-default contrast/gamma.
  SurfaceProps()
      : SurfaceProps(0, PixelGeometry::kUnknown) {
  }
  // Platform-default contrast/gamma.
  SurfaceProps(std::uint32_t flags, PixelGeometry pixel_geometry)
      : SurfaceProps(flags, pixel_geometry, kGammaContrast, kGammaExponent) {
  }
  // Custom contrast/gamma parameters.
  SurfaceProps(std::uint32_t flags, PixelGeometry pixel_geometry, float text_contrast, float text_gamma)
      : flags_(flags), pixel_geometry_(pixel_geometry), text_contrast_(text_contrast), text_gamma_(text_gamma) {
  }

  SurfaceProps CloneWithPixelGeometry(PixelGeometry new_pixel_geometry) const {
    return SurfaceProps(flags_, new_pixel_geometry, text_contrast_, text_gamma_);
  }

  static constexpr float kMaxContrastInclusive = 1;
  static constexpr float kMinContrastInclusive = 0;
  static constexpr float kMaxGammaExclusive = 4;
  static constexpr float kMinGammaInclusive = 0;

  std::uint32_t GetFlags() const {
    return flags_;
  }
  PixelGeometry GetPixelGeometry() const {
    return pixel_geometry_;
  }
  float TextContrast() const {
    return text_contrast_;
  }
  float TextGamma() const {
    return text_gamma_;
  }

  bool IsUseDeviceIndependentFonts() const {
    return (flags_ & kUseDeviceIndependentFonts_Flag) != 0;
  }

  bool IsAlwaysDither() const {
    return (flags_ & kAlwaysDither_Flag) != 0;
  }

  bool operator==(const SurfaceProps& that) const {
    return flags_ == that.flags_ && pixel_geometry_ == that.pixel_geometry_ &&
           text_contrast_ == that.text_contrast_ && text_gamma_ == that.text_gamma_;
  }

private:
  std::uint32_t flags_;
  PixelGeometry pixel_geometry_;

  // This gamma value is specifically about blending of mask coverage. The
  // surface also has a color space, but that applies to the colors.
  float text_contrast_;
  float text_gamma_;
};

} // namespace bkit
