// Ported from: skia/src/core/SkBlurEngine.cpp (Gaussian kernel and radius)
// Ported from: skia/src/effects/imagefilters/SkDropShadowImageFilter.cpp
// Ported from: skia/src/effects/imagefilters/SkMergeImageFilter.cpp
// The existing CPU canvas uses float pixels; filter sampling has the same
// precision limitation as that canvas rather than Skia's integer raster backend.
#include "image_filter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "blend_mode.h"

namespace bkfont {
namespace {

PMColor4f Add(PMColor4f a, PMColor4f b) {
  return {a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a};
}
IntRect Intersect(IntRect a, IntRect b) {
  IntRect r = IntRect::MakeLTRB(std::max(a.left, b.left), std::max(a.top, b.top),
                                std::min(a.right, b.right), std::min(a.bottom, b.bottom));
  return r.IsEmpty() ? IntRect() : r;
}
ScalarRect ToRect(IntRect r) {
  return ScalarRect::MakeLTRB(static_cast<float>(r.left), static_cast<float>(r.top),
                              static_cast<float>(r.right), static_cast<float>(r.bottom));
}
IntRect RoundBounds(ScalarRect r) {
  if (!r.IsFinite() || r.IsEmpty()) return {};
  // Keep dimensions representable by the raster backend.
  constexpr float limit = static_cast<float>(std::numeric_limits<int>::max() / 4);
  if (r.left < -limit || r.top < -limit || r.right > limit || r.bottom > limit) return {};
  return IntRect::MakeLTRB(static_cast<int>(std::floor(r.left)), static_cast<int>(std::floor(r.top)),
                            static_cast<int>(std::ceil(r.right)), static_cast<int>(std::ceil(r.bottom)));
}
FilterImage Allocate(IntRect bounds) {
  FilterImage image;
  if (bounds.IsEmpty()) return image;
  image.bounds = bounds;
  image.pixels.resize(static_cast<std::size_t>(bounds.Width()) * bounds.Height());
  return image;
}

struct BlurAxis {
  ScalarPoint direction;
  float sigma;
  int Radius() const { return sigma <= 0.03f ? 0 : static_cast<int>(std::ceil(3 * sigma)); }
};
BlurAxis MakeAxis(float x, float y, float sigma) {
  const float length = std::hypot(x, y);
  if (!(length > 0) || !std::isfinite(length)) return {{1, 0}, 0};
  return {{x / length, y / length}, std::min(sigma * length, 532.0f)};
}

// SkShaderBlurAlgorithm::Compute1DBlurKernel: normalized, truncated at 3 sigma.
std::vector<float> GaussianKernel(const BlurAxis& axis) {
  const int radius = axis.Radius();
  std::vector<float> kernel(2 * radius + 1, 1);
  if (!radius) return kernel;
  const float denominator = 1 / (2 * axis.sigma * axis.sigma);
  float sum = 0;
  for (int i = -radius; i <= radius; ++i) {
    kernel[i + radius] = std::exp(-static_cast<float>(i * i) * denominator);
    sum += kernel[i + radius];
  }
  const float scale = 1 / sum;
  for (float& value : kernel) value *= scale;
  return kernel;
}

// A drop shadow consumes only source alpha. Keeping the convolution in one
// channel avoids allocating and sampling three color channels that are thrown
// away by the subsequent colorization.
struct AlphaImage {
  IntRect bounds;
  std::vector<float> pixels;
};

template <typename ImageType>
float AlphaAt(const ImageType& image, int x, int y) {
  if (x < image.bounds.left || x >= image.bounds.right || y < image.bounds.top || y >= image.bounds.bottom) return 0;
  const auto& pixel = image.pixels.data()[static_cast<std::size_t>(y - image.bounds.top) * image.bounds.Width() +
                                          x - image.bounds.left];
  if constexpr (requires { pixel.a; }) return pixel.a;
  else return pixel;
}

template <typename ImageType>
float SampleAlpha(const ImageType& image, float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < image.bounds.left - 1.0f || x > image.bounds.right ||
      y < image.bounds.top - 1.0f || y > image.bounds.bottom) return 0;
  const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
  const float fx = x - ix, fy = y - iy;
  return (AlphaAt(image, ix, iy) * ((1 - fx) * (1 - fy)) + AlphaAt(image, ix + 1, iy) * (fx * (1 - fy))) +
         (AlphaAt(image, ix, iy + 1) * ((1 - fx) * fy) + AlphaAt(image, ix + 1, iy + 1) * (fx * fy));
}

template <typename ImageType>
AlphaImage BlurAlpha(const ImageType& src, const BlurAxis& axis, IntRect desired) {
  if (src.bounds.IsEmpty() || desired.IsEmpty()) return {};
  ScalarRect bounds = ToRect(src.bounds);
  const int radius = axis.Radius();
  bounds.Outset(radius * std::abs(axis.direction.x), radius * std::abs(axis.direction.y));
  AlphaImage result;
  result.bounds = Intersect(RoundBounds(bounds), desired);
  if (result.bounds.IsEmpty()) return result;
  result.pixels.resize(static_cast<std::size_t>(result.bounds.Width()) * result.bounds.Height());
  const auto kernel = GaussianKernel(axis);
  const float* weights = kernel.data();
  const bool horizontal = std::abs(axis.direction.x) == 1 && axis.direction.y == 0;
  const bool vertical = std::abs(axis.direction.y) == 1 && axis.direction.x == 0;
  for (int y = result.bounds.top; y < result.bounds.bottom; ++y) {
    float* dst = result.pixels.data() + static_cast<std::size_t>(y - result.bounds.top) * result.bounds.Width();
    for (int x = result.bounds.left; x < result.bounds.right; ++x) {
      float alpha = 0;
      if (horizontal || vertical) {
        // Integer taps for the usual scale/translate or 90-degree rotation.
        // The kernel and decal edges are unchanged; no bilinear sampling is
        // needed on this path. Sheared/oblique axes use the fallback below.
        const int direction = (horizontal ? axis.direction.x : axis.direction.y) > 0 ? 1 : -1;
        const int p = horizontal ? x : y;
        const int begin = horizontal ? src.bounds.left : src.bounds.top;
        const int end = horizontal ? src.bounds.right : src.bounds.bottom;
        const int first = std::max(-radius, direction > 0 ? begin - p : p - end + 1);
        const int last = std::min(radius, direction > 0 ? end - p - 1 : p - begin);
        if (first <= last && (horizontal ? y >= src.bounds.top && y < src.bounds.bottom :
                                          x >= src.bounds.left && x < src.bounds.right)) {
          const int sx = horizontal ? x + first * direction : x;
          const int sy = horizontal ? y : y + first * direction;
          const auto* pixels = src.pixels.data() + static_cast<std::size_t>(sy - src.bounds.top) * src.bounds.Width() +
                                  sx - src.bounds.left;
          const std::ptrdiff_t step = direction * (horizontal ? 1 : static_cast<std::ptrdiff_t>(src.bounds.Width()));
          for (int i = first; i <= last; ++i) {
            const auto& pixel = pixels[(i - first) * step];
            if constexpr (requires { pixel.a; }) alpha += pixel.a * weights[i + radius];
            else alpha += pixel * weights[i + radius];
          }
        }
      } else {
        for (int i = -radius; i <= radius; ++i) {
          alpha += SampleAlpha(src, x + i * axis.direction.x, y + i * axis.direction.y) * weights[i + radius];
        }
      }
      dst[x - result.bounds.left] = alpha;
    }
  }
  return result;
}

class DropShadowFilter final : public ImageFilter {
public:
  DropShadowFilter(float dx, float dy, float sx, float sy, Color4f color, bool only)
      : offset_{dx, dy}, sigma_{sx, sy}, color_(color), shadow_only_(only) {}

  IntRect RequiredInput(const IntRect& output, const ScalarMatrix& matrix) const override {
    if (output.IsEmpty()) return {};
    const auto [x_axis, y_axis] = Axes(matrix);
    const ScalarPoint offset = Offset(matrix);
    ScalarRect input = ToRect(output);
    input.Offset(-offset.x, -offset.y);
    input.Outset(x_axis.Radius() * std::abs(x_axis.direction.x) + y_axis.Radius() * std::abs(y_axis.direction.x) + 1,
                  x_axis.Radius() * std::abs(x_axis.direction.y) + y_axis.Radius() * std::abs(y_axis.direction.y) + 1);
    if (!shadow_only_) input.Join(ToRect(output));
    return RoundBounds(input);
  }

  FilterImage Apply(const FilterImage& source, const ScalarMatrix& matrix, const IntRect& output) const override {
    if (source.bounds.IsEmpty() || output.IsEmpty()) return {};
    const auto [x_axis, y_axis] = Axes(matrix);
    const ScalarPoint offset = Offset(matrix);
    ScalarRect desired = ToRect(output);
    desired.Offset(-offset.x, -offset.y);
    desired.Outset(1, 1); // linear sampling of the fractional offset
    ScalarRect intermediate = desired;
    intermediate.Outset(y_axis.Radius() * std::abs(y_axis.direction.x) + 1,
                         y_axis.Radius() * std::abs(y_axis.direction.y) + 1);
    AlphaImage horizontal = BlurAlpha(source, x_axis, RoundBounds(intermediate));
    AlphaImage blurred = BlurAlpha(horizontal, y_axis, RoundBounds(desired));
    ScalarRect bounds = ToRect(blurred.bounds);
    bounds.Offset(offset.x, offset.y);
    if (!shadow_only_) bounds.Join(ToRect(source.bounds));
    FilterImage result = Allocate(Intersect(RoundBounds(bounds), output));
    const PMColor4f color = color_.Premul();
    for (int y = result.bounds.top; y < result.bounds.bottom; ++y) {
      for (int x = result.bounds.left; x < result.bounds.right; ++x) {
        PMColor4f pixel = color * SampleAlpha(blurred, x - offset.x, y - offset.y);
        if (!shadow_only_) pixel = BlendPixel(BlendMode::kSrcOver, source.At(x, y), pixel);
        result.pixels[static_cast<std::size_t>(y - result.bounds.top) * result.bounds.Width() + x - result.bounds.left] = pixel;
      }
    }
    return result;
  }

private:
  std::pair<BlurAxis, BlurAxis> Axes(const ScalarMatrix& m) const {
    return {MakeAxis(m.GetScaleX(), m.GetSkewY(), sigma_.x), MakeAxis(m.GetSkewX(), m.GetScaleY(), sigma_.y)};
  }
  ScalarPoint Offset(const ScalarMatrix& m) const {
    return {m.GetScaleX() * offset_.x + m.GetSkewX() * offset_.y,
            m.GetSkewY() * offset_.x + m.GetScaleY() * offset_.y};
  }
  ScalarPoint offset_, sigma_;
  Color4f color_;
  bool shadow_only_;
};

class MergeFilter final : public ImageFilter {
public:
  explicit MergeFilter(std::span<const std::shared_ptr<const ImageFilter>> inputs)
      : inputs_(inputs.begin(), inputs.end()) {}
  IntRect RequiredInput(const IntRect& output, const ScalarMatrix& matrix) const override {
    ScalarRect result;
    for (const auto& input : inputs_) result.Join(ToRect(input ? input->RequiredInput(output, matrix) : output));
    return RoundBounds(result);
  }
  FilterImage Apply(const FilterImage& source, const ScalarMatrix& matrix, const IntRect& output) const override {
    std::vector<FilterImage> children;
    ScalarRect bounds;
    for (const auto& input : inputs_) {
      children.push_back(input ? input->Apply(source, matrix, output) : source);
      bounds.Join(ToRect(children.back().bounds));
    }
    FilterImage result = Allocate(Intersect(RoundBounds(bounds), output));
    for (const auto& child : children) {
      const IntRect area = Intersect(child.bounds, result.bounds);
      for (int y = area.top; y < area.bottom; ++y) {
        for (int x = area.left; x < area.right; ++x) {
          auto& dst = result.pixels[static_cast<std::size_t>(y - result.bounds.top) * result.bounds.Width() + x - result.bounds.left];
          dst = BlendPixel(BlendMode::kSrcOver, child.At(x, y), dst);
        }
      }
    }
    return result;
  }
private:
  std::vector<std::shared_ptr<const ImageFilter>> inputs_;
};
} // namespace

PMColor4f FilterImage::At(int x, int y) const {
  if (x < bounds.left || x >= bounds.right || y < bounds.top || y >= bounds.bottom) return {};
  return pixels[static_cast<std::size_t>(y - bounds.top) * bounds.Width() + x - bounds.left];
}
PMColor4f FilterImage::Sample(float x, float y) const {
  if (!std::isfinite(x) || !std::isfinite(y) || x < bounds.left - 1.0f || x > bounds.right ||
      y < bounds.top - 1.0f || y > bounds.bottom) return {};
  const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
  const float fx = x - ix, fy = y - iy;
  return Add(Add(At(ix, iy) * ((1 - fx) * (1 - fy)), At(ix + 1, iy) * (fx * (1 - fy))),
             Add(At(ix, iy + 1) * ((1 - fx) * fy), At(ix + 1, iy + 1) * (fx * fy)));
}
std::shared_ptr<const ImageFilter> ImageFilter::DropShadow(float dx, float dy, float sx, float sy,
                                                         Color4f color, bool only) {
  if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(sx) || !std::isfinite(sy) || sx < 0 || sy < 0) return nullptr;
  return std::make_shared<DropShadowFilter>(dx, dy, sx, sy, color, only);
}
std::shared_ptr<const ImageFilter> ImageFilter::Merge(std::span<const std::shared_ptr<const ImageFilter>> inputs) {
  if (inputs.empty()) return nullptr;
  if (inputs.size() == 1) return inputs.front();
  return std::make_shared<MergeFilter>(inputs);
}
} // namespace bkfont
