// Ported from: skia/include/core/SkShader.h
// Ported from: skia/include/core/SkTileMode.h
// Ported from: skia/include/core/SkSamplingOptions.h
// Ported from: skia/include/effects/SkGradientShader.h
// Ported from: skia/src/shaders/gradients/SkGradientBaseShader.cpp
// Ported from: skia/src/shaders/gradients/SkLinearGradient.cpp
// Ported from: skia/src/shaders/gradients/SkRadialGradient.cpp
// Ported from: skia/src/shaders/gradients/SkConicalGradient.cpp
// Ported from: skia/src/shaders/gradients/SkSweepGradient.cpp
// Ported from: skia/src/shaders/SkImageShader.cpp

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "color4f.h"
#include "matrix.h"
#include "pixmap.h"
#include "rect.h"

namespace bkfont {

// SkTileMode.
enum class TileMode {
  kClamp,
  kRepeat,
  kMirror,
  kDecal,
};

// SkFilterMode, SkMipmapMode and SkSamplingOptions without cubic resampling.
enum class FilterMode {
  kNearest,
  kLinear,
};
enum class MipmapMode {
  kNone,
  kNearest,
  kLinear,
};
struct SamplingOptions {
  FilterMode filter = FilterMode::kNearest;
  MipmapMode mipmap = MipmapMode::kNone;
};

// SkShader, evaluated on the CPU. Shading happens in the legacy sRGB-encoded
// space of N32 surfaces and produces premultiplied colors.
class Shader {
public:
  virtual ~Shader();

  class Context {
  public:
    virtual ~Context();
    // Shades count pixels of row y starting at column x. The pixel centers
    // are mapped to the shader's space with the inverse of the matrix given
    // to MakeContext.
    virtual void ShadeSpan(int x, int y, int count, PMColor4f* out) = 0;
  };

  // Returns null when nothing should be drawn, as when the matrix is not
  // invertible or the shader is empty. The paint color modulates the
  // output: its alpha always, and its color for alpha-only images.
  virtual std::unique_ptr<Context> MakeContext(const ScalarMatrix& ctm, const Color4f& paint_color) const = 0;

  // SkShader::isOpaque.
  virtual bool IsOpaque() const {
    return false;
  }

  // Whether SkShaderBase::makeContext returns a legacy shader context for
  // ctm, which lets SkBlitter::Choose use the legacy N32 shader blitter
  // instead of the raster pipeline. Only N32 image shaders have one.
  virtual bool CanMakeLegacyContext(const ScalarMatrix&) const {
    return false;
  }

  // SkShaderBase::asLuminanceColor. Returns an opaque representative color
  // for glyph mask gamma correction. A null pointer only queries support.
  virtual bool AsLuminanceColor(Color4f*) const {
    return false;
  }
};

// SkShaders::Color and SkShaders::Empty.
std::shared_ptr<const Shader> MakeColorShader(const Color4f& color);
std::shared_ptr<const Shader> MakeEmptyShader();

// SkShader::makeWithLocalMatrix: an SkLocalMatrixShader that concatenates
// local_matrix after the ctm and before the shader's own local matrix.
std::shared_ptr<const Shader> MakeWithLocalMatrix(std::shared_ptr<const Shader> shader,
                                                  const ScalarMatrix& local_matrix);

// SkGradientShader with Interpolation{InPremul::kNo, ColorSpace::kSRGB,
// HueMethod::kShorter}, the only interpolation used here: colors are
// interpolated unpremultiplied in sRGB and premultiplied afterwards. A null
// result means the factory failed, as upstream.
class GradientShader {
public:
  static std::shared_ptr<const Shader> MakeLinear(const ScalarPoint pts[2],
                                                  std::span<const Color4f> colors,
                                                  const float* pos,
                                                  TileMode mode);

  static std::shared_ptr<const Shader> MakeRadial(const ScalarPoint& center, float radius,
                                                  std::span<const Color4f> colors,
                                                  const float* pos,
                                                  TileMode mode);

  static std::shared_ptr<const Shader> MakeTwoPointConical(const ScalarPoint& start, float start_radius,
                                                           const ScalarPoint& end, float end_radius,
                                                           std::span<const Color4f> colors,
                                                           const float* pos,
                                                           TileMode mode);

  // Angles are in degrees, measured clockwise in the y-down space.
  static std::shared_ptr<const Shader> MakeSweep(float cx, float cy,
                                                 std::span<const Color4f> colors,
                                                 const float* pos,
                                                 TileMode mode,
                                                 float start_angle, float end_angle);
};

class Image;

// SkImageShader with kClamp tiling in both axes, as SkCanvas::drawImage
// uses. Cubic resampling and anisotropic filtering are not ported.
std::shared_ptr<const Shader> MakeImageShader(std::shared_ptr<const Image> image,
                                              const SamplingOptions& sampling,
                                              const ScalarMatrix& local_matrix);

} // namespace bkfont
